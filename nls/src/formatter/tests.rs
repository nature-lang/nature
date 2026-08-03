use super::format_source;

fn assert_format(src: &str, expected: &str) {
    let formatted = format_source(src).expect("format succeeds");
    assert_eq!(formatted, expected);
    assert_eq!(format_source(&formatted).unwrap(), formatted);
}

#[test]
fn format_simple_function() {
    assert_format(
        "fn add(a:int,b:int):int{ return a+b }",
        "fn add(a: int, b: int): int {\n    return a + b\n}\n",
    );
}

#[test]
fn format_parameter_and_return_type_spacing() {
    assert_format(
        "fn add(a:int,b:int):int{ return a+b }",
        "fn add(a: int, b: int): int {\n    return a + b\n}\n",
    );
}

#[test]
fn format_comma_separated_parameters() {
    assert_format(
        "fn merge(x:int,y:int,z:int):int{x=x+y+z}",
        "fn merge(x: int, y: int, z: int): int {\n    x = x + y + z\n}\n",
    );
}

#[test]
fn format_binary_operator_spacing() {
    assert_format(
        "let x=1+2*3-4/5%6==7&&8||9;",
        "let x = 1 + 2 * 3 - 4 / 5 % 6 == 7 && 8 || 9\n",
    );
}

#[test]
fn format_if_else_blocks() {
    assert_format(
        "if(x>0){x=x-1}else{x=x+1}",
        "if (x > 0) {\n    x = x - 1\n} else {\n    x = x + 1\n}\n",
    );
}

#[test]
fn format_space_before_left_curly_after_condition() {
    assert_format(
        "if(a>0){return a}else{return -a}",
        "if (a > 0) {\n    return a\n} else {\n    return -a\n}\n",
    );
}

#[test]
fn format_nested_blocks_with_indentation() {
    assert_format(
        "fn main(){if(x){y=1}else{y=2}}",
        "fn main() {\n    if (x) {\n        y = 1\n    } else {\n        y = 2\n    }\n}\n",
    );
}

#[test]
fn format_empty_function_body() {
    assert_format(
        "fn empty(){}",
        "fn empty() {}\n",
    );
}

#[test]
fn format_space_before_left_curly_after_word() {
    assert_format(
        "for true{break}",
        "for true {\n    break\n}\n",
    );
}

#[test]
fn format_match_expression_with_space_before_block() {
    assert_format(
        "fn demo(a:int):int{var r=match{a==1 -> 10 _ -> 20} return r}",
        "fn demo(a: int): int {\n    var r = match {\n        a == 1 -> 10\n        _ -> 20\n    }\n    return r\n}\n",
    );
}

#[test]
fn format_catch_after_postfix_expression() {
    assert_format(
        "list[5]catch err {0}\nrisky(10,0)catch err2 {0}",
        "list[5] catch err {\n    0\n}\nrisky(10, 0) catch err2 {\n    0\n}\n",
    );
}

#[test]
fn format_generic_type_arguments_without_inner_spaces() {
    assert_format(
        "fn demo():void{var ch=chan<string>.new();nullable<int> maybe=null}",
        "fn demo(): void {\n    var ch = chan<string>.new()\n    nullable<int> maybe = null\n}\n",
    );
}

#[test]
fn format_generic_type_alias_with_assignment_spacing() {
    assert_format(
        "type nullable<T>=T|null\ntype pair<T,E>=T|E|null",
        "type nullable<T> = T | null\ntype pair<T, E> = T | E | null\n",
    );
}

#[test]
fn format_is_optional_and_errable_spacing() {
    assert_format(
        "fn use(testable?test):void!{if(test is testable){return}}",
        "fn use(testable? test): void! {\n    if (test is testable) {\n        return\n    }\n}\n",
    );
}

#[test]
fn format_for_header_on_single_line() {
    assert_format(
        "for int i=0;i<3;i+=1{break}",
        "for int i = 0; i < 3; i += 1 {\n    break\n}\n",
    );
}

#[test]
fn format_comma_and_postfix_spacing() {
    assert_format(
        "println('x',test.nice(),true,[1,2])\nprint(err.msg())0",
        "println('x', test.nice(), true, [1, 2])\nprint(err.msg())\n0\n",
    );
}

#[test]
fn format_receiver_and_unary_prefix_operators() {
    assert_format(
        "fn t.nice(&self):int{return -1}\nfn t.show(*self){let x=!true;println(foo(-4),&self)}",
        "fn t.nice(&self): int {\n    return -1\n}\nfn t.show(*self) {\n    let x = !true\n    println(foo(-4), &self)\n}\n",
    );
}

#[test]
fn normalize_spacing_around_colon_and_comma() {
    assert_format(
        "fn foo(a :int ,b: int):int{ return a+b }",
        "fn foo(a: int, b: int): int {\n    return a + b\n}\n",
    );
}

#[test]
fn format_map_literal_key_value_spacing() {
    assert_format(
        "var m={'a':[1,2],'b':{3,4}}",
        "var m = {\n    'a': [1, 2], 'b': {\n        3, 4\n    }\n}\n",
    );
}

#[test]
fn format_enum_members_one_per_line_without_extra_indent_space() {
    assert_format(
        "type Color=enum:u16{RED=1,GREEN,BLUE=7,}",
        "type Color = enum: u16 {\n    RED = 1,\n    GREEN,\n    BLUE = 7,\n}\n",
    );
}

#[test]
fn format_multiple_statements_and_newlines() {
    assert_format(
        "fn main(){let x=1;let y=2;return x+y}",
        "fn main() {\n    let x = 1\n    let y = 2\n    return x + y\n}\n",
    );
}

#[test]
fn format_block_comment_positioning() {
    assert_format(
        "fn main(){/*comment*/let x=1;}",
        "fn main() {\n    /*comment*/\n    let x = 1\n}\n",
    );
}

#[test]
fn format_unary_operator_spacing() {
    assert_format(
        "let x=-1;let y=!x;",
        "let x = -1\nlet y = !x\n",
    );
}

#[test]
fn preserve_string_literals_and_keywords() {
    assert_format(
        "let s = \"hello:world\";",
        "let s = \"hello:world\"\n",
    );
}

#[test]
fn preserve_comments_and_idempotence() {
    let src = "fn main(){ // hello\n let x=1; // value\n }";
    let formatted = format_source(src).expect("format succeeds");
    assert!(formatted.contains("// hello\n"));
    assert!(formatted.contains("// value\n"));
    assert_eq!(format_source(&formatted).unwrap(), formatted);
}