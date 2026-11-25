import lldb
import os

def ping(frame,bp_loc,internal_dict):
    print("MATERIAL breakpoint",frame.GetFunctionName(), flush=True)
    o=lldb.SBExpressionOptions();o.SetIgnoreBreakpoints(True);o.SetTimeoutInMicroSeconds(4000000)
    p=frame.EvaluateExpression("(void)materialProbe((void*)$x0)",o)
    if p.GetError().Fail():
        print("MATERIAL inspection", p.GetError(),flush=True)
    return False

def hit(frame,bp_loc,internal_dict):
    print("MATERIAL route",frame.GetFunctionName(),flush=True)
    return False

def setup(debugger,command,result,internal_dict):
    target=debugger.GetSelectedTarget()
    b=target.BreakpointCreateByRegex("triggerDocumentAction\(sed::MainWindow")
    b.SetScriptCallbackFunction(__name__+".ping")
    print("MATERIAL helper locations",b.GetNumLocations(),flush=True)
    for name in ["triggerNativeTextEditorUndoRedo","triggerActiveCogScriptEditorUndoRedo","triggerActiveResourceEditorUndoRedo"]:
        route=target.BreakpointCreateByRegex(name)
        route.SetScriptCallbackFunction(__name__+".hit")

def __lldb_init_module(debugger,internal_dict):
    debugger.HandleCommand("command script add -f "+__name__+".setup trace-material")
