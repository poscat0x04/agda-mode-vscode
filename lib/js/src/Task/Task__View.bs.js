// Generated by BUCKLESCRIPT, PLEASE EDIT WITH CARE
'use strict';

var Task$AgdaModeVscode = require("./Task.bs.js");
var Caml_chrome_debugger = require("bs-platform/lib/js/caml_chrome_debugger.js");
var State$AgdaModeVscode = require("../State.bs.js");

function Impl(Editor) {
  var Task = Task$AgdaModeVscode.Impl(Editor);
  var State = State$AgdaModeVscode.Impl(Editor);
  var handle = function (param) {
    if (param) {
      return /* :: */Caml_chrome_debugger.simpleVariant("::", [
                /* Terminate */0,
                /* [] */0
              ]);
    } else {
      return /* [] */0;
    }
  };
  return {
          Task: Task,
          State: State,
          handle: handle
        };
}

exports.Impl = Impl;
/* Task-AgdaModeVscode Not a pure module */