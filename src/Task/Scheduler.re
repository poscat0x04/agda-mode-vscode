open Belt;

module Runner = {
  type status =
    | Busy
    | Idle;

  type t('a) = {
    mutable queue: array('a),
    mutable status,
    // the work horse
    execute: 'a => Promise.t(unit),
    // invoke `terminate` to resolve `terminationPromise`
    terminationPromise: Promise.t(unit),
    terminate: unit => unit,
    // this flag is set to True when the runner should be terminated despite that it's still running
    // transfer the responsibility of invoking `terminate` to the runner
    mutable shouldTerminate: bool,
  };

  let make = execute => {
    let (promise, resolve) = Promise.pending();
    {
      queue: [||],
      status: Idle,
      execute,
      terminationPromise: promise,
      terminate: resolve,
      shouldTerminate: false,
    };
  };

  let rec run = (self: t('a)): Promise.t(unit) =>
    switch (self.status) {
    // only one `run` should be running at a time
    | Busy =>
      Js.log("[ run ] Busy");
      Promise.resolved();
    | Idle =>
      let nextTasks = Js.Array.shift(self.queue);
      (
        switch (nextTasks) {
        | None =>
          Js.log("[ run ] Idle, no tasks");
          Promise.resolved();
        | Some(task) =>
          Js.log("[ run ] Idle");
          self.status = Busy;
          self.execute(task)
          ->Promise.tap(_ => {self.status = Idle})
          ->Promise.flatMap(() => {run(self)});
        }
      )
      // see if the runner is responsible of invoking `terminate`
      // after finished executing all tasks in the queue
      ->Promise.tap(() =>
          if (self.shouldTerminate) {
            self.terminate();
          }
        );
    };

  let push = (self, x: 'a) => {
    // push a new task to the queue
    Js.Array.push(x, self.queue)->ignore;
    // kick start the runner
    run(self);
  };

  let pushMany = (self: t('a), xs: array('a)): Promise.t(unit) => {
    // concat tasks to the back of the queue
    self.queue = Js.Array.concat(self.queue, xs);
    // kick start the runner
    run(self);
  };

  // If the runner is currently Idle,
  // then resolve the termination promise immediately
  // else set `shouldTerminate` and wait for the runner to resolve the termination promise
  let terminate = self =>
    switch (self.status) {
    | Idle => self.terminate()
    | Busy => self.shouldTerminate = true
    };
};

module Impl = (Editor: Sig.Editor) => {
  module ErrorHandler = Handle__Error.Impl(Editor);
  module ViewHandler = Handle__View.Impl(Editor);
  module CommandHandler = Handle__Command.Impl(Editor);
  module GoalHandler = Handle__Goal.Impl(Editor);
  module ResponseHandler = Handle__Response.Impl(Editor);
  module Task = Task.Impl(Editor);
  module State = State.Impl(Editor);
  module Request = Request.Impl(Editor);

  type t = Runner.t(Command.t);

  // Task Runner
  let dispatchCommand = (self, command) => Runner.push(self, command);

  let rec sendRequest =
          (state, request: Request.t): Promise.t(array(Request.t)) => {
    // Task.SendRequest will be deferred and executed until the current request is handled
    let derivedRequests = ref([||]);

    let runner = Runner.make(task => {runTask(state, task)});

    // handle of the connection response listener
    let handle = ref(None);
    let handler =
      fun
      | Error(error) => {
          let tasks = ErrorHandler.handle(Error.Connection(error));
          Runner.pushMany(runner, List.toArray(tasks))
          ->Promise.get(() => {Runner.terminate(runner)});
        }
      | Ok(Parser.Incr.Event.Yield(Error(error))) => {
          let tasks = ErrorHandler.handle(Error.Parser(error));
          Runner.pushMany(runner, List.toArray(tasks))
          ->Promise.get(() => {Runner.terminate(runner)});
        }
      | Ok(Yield(Ok(response))) => {
          // Task.SendRequest are filtered out
          let otherTasks =
            List.toArray(ResponseHandler.handle(response))
            ->Array.keep(
                fun
                | Task.SendRequest(req) => {
                    Js.Array.push(req, derivedRequests^)->ignore;
                    false;
                  }
                | _ => true,
              );
          Runner.pushMany(runner, otherTasks)->ignore;
        }
      | Ok(Stop) => Runner.terminate(runner);

    let promise = runner.terminationPromise;

    state
    ->State.sendRequest(request)
    ->Promise.flatMap(
        fun
        | Ok(connection) => {
            handle := Some(connection.Connection.emitter.on(handler));
            promise;
          }
        | Error(error) => {
            let tasks = ErrorHandler.handle(error);
            runTasks(state, tasks)->Promise.flatMap(() => promise);
          },
      )
    ->Promise.tap(() => (handle^)->Option.forEach(f => f()))
    ->Promise.map(() => derivedRequests^);
  }
  and sendRequests = (state, requests: list(Request.t)): Promise.t(unit) =>
    switch (requests) {
    | [] => Promise.resolved()
    | [x, ...xs] =>
      sendRequest(state, x)
      ->Promise.flatMap(xs' =>
          sendRequests(state, List.concat(List.fromArray(xs'), xs))
        )
    }
  and runTask = (state, task: Task.t): Promise.t(unit) => {
    switch (task) {
    | Task.Terminate =>
      Js.log("[ task ][ terminate ] ");
      State.destroy(state);
    | WithState(callback) =>
      Js.log("[ task ][ with state ] ");
      callback(state)->Promise.flatMap(runTasks(state));
    | Goal(action) =>
      Js.log("[ task ][ goal ] ");
      let tasks = GoalHandler.handle(action);
      runTasks(state, tasks);
    | SendRequest(request) =>
      Js.log("[ task ][ send request ]");
      sendRequests(state, [request]);
    | ViewReq(request) =>
      Js.log("[ task ][ view request ] ");
      state->State.sendRequestToView(request);
    | ViewRes(response) =>
      Js.log("[ task ][ view response ] ");
      let tasks = ViewHandler.handle(response);
      runTasks(state, tasks);
    | Error(error) =>
      Js.log("[ task ][ view error ] ");
      let tasks = ErrorHandler.handle(error);
      runTasks(state, tasks);
    | Debug(message) =>
      Js.log("[ debug ] " ++ message);
      runTasks(state, [Task.displayWarning("Debug", Some(message))]);
    };
  }
  and runTasks = (state, tasks: list(Task.t)): Promise.t(unit) =>
    switch (tasks) {
    | [] => Promise.resolved()
    | [x, ...xs] =>
      runTask(state, x)->Promise.flatMap(() => runTasks(state, xs))
    };

  let make = state => {
    Runner.make(command => {
      let tasks = CommandHandler.handle(command);
      runTasks(state, tasks);
    });
  };

  // destroy only after all tasks have been executed
  let destroy = (runner: Runner.t(Command.t)): Promise.t(unit) =>
    runner.terminationPromise;
};