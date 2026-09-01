// The error type is x mode so that .x can catch an error and read its message.
// The mode is part of a fn type, so an interface and its implementation have to agree:
// leaving throwable in .n would make msg() uncallable from .x. Moving it costs .n
// nothing, because a .n to .x call is allowed and errort is the only implementor.
//
// errorf stays in error.n, it needs fmt.sprintf.

pub type throwable = interface{
	fn msg():string
}

pub type trace_t = struct{
    string path
    string ident
    int line
    int column
}

pub type errort:throwable = struct{
	string message
	bool is_panic
}

pub fn errort.msg(&self):string {
	return self.message
}

pub type errable<T> = errort|T
