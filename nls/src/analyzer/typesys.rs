use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
};

use std::collections::hash_map::DefaultHasher;
use std::collections::HashSet;
use std::hash::{Hash, Hasher};

use super::{
    common::{AnalyzerError, AstCall, AstNode, Expr, Stmt, Type, TypeFn, TypedefStmt, VarDeclExpr},
    symbol::{NodeId, SymbolTable},
};
use crate::{
    analyzer::{common::*, symbol::SymbolKind},
    project::Module,
    utils::{errors_push, format_generics_ident, format_impl_ident},
};

const EQUATABLE_IDENT: &str = "equatable";
const COMPARABLE_IDENT: &str = "comparable";
const ADDABLE_IDENT: &str = "addable";
const NUMERIC_IDENT: &str = "numeric";
const NONVOID_IDENT: &str = "nonvoid";

#[derive(Debug, Clone)]
pub struct GenericSpecialFnClone {
    // default 是 none, clone 过程中, 当 global fn clone 完成后，将 clone 完成的 global fn 赋值给 global_parent
    global_parent: Option<Arc<Mutex<AstFnDef>>>,
}

impl GenericSpecialFnClone {
    pub fn deep_clone(&mut self, fn_mutex: &Arc<Mutex<AstFnDef>>) -> Arc<Mutex<AstFnDef>> {
        let fn_def = fn_mutex.lock().unwrap();
        let mut fn_def_clone = fn_def.clone();

        // type 中不包含 arc, 所以可以直接进行 clone
        fn_def_clone.type_ = fn_def.type_.clone();
        fn_def_clone.return_type = fn_def.return_type.clone();
        // params 中包含 arc, 所以需要解构后进行 clone
        fn_def_clone.params = fn_def
            .params
            .iter()
            .map(|param| {
                let param_clone = param.lock().unwrap().clone();
                Arc::new(Mutex::new(param_clone))
            })
            .collect();

        // 清空 generics 标识
        fn_def_clone.is_generics = false;

        // 递归进行 body 的 clone
        fn_def_clone.body = self.clone_body(&fn_def.body);
        fn_def_clone.global_parent = None;

        // 重新完善 children 和 parent 关系
        if fn_def_clone.is_local {
            debug_assert!(self.global_parent.is_some());
            fn_def_clone.global_parent = self.global_parent.clone();
            {
                let mut global_parent = self.global_parent.as_ref().unwrap().lock().unwrap();
                let result = Arc::new(Mutex::new(fn_def_clone));
                global_parent.local_children.push(result.clone());
                return result;
            }
        } else {
            let result = Arc::new(Mutex::new(fn_def_clone));
            self.global_parent = Some(result.clone());
            return result;
        }
    }

    fn clone_body(&mut self, body: &AstBody) -> AstBody {
        let stmts = body.stmts.iter().map(|stmt| Box::new(self.clone_stmt(stmt))).collect();
        return AstBody {
            stmts,
            start: body.start,
            end: body.end,
        };
    }

    fn clone_expr(&mut self, expr: &Expr) -> Expr {
        let node = match &expr.node {
            AstNode::Literal(kind, value) => AstNode::Literal(kind.clone(), value.clone()),
            AstNode::Ident(ident, symbol_id) => AstNode::Ident(ident.clone(), symbol_id.clone()),
            AstNode::EnvAccess(index, ident, symbol_id) => AstNode::EnvAccess(*index, ident.clone(), symbol_id.clone()),

            AstNode::Binary(op, left, right) => AstNode::Binary(op.clone(), Box::new(self.clone_expr(left)), Box::new(self.clone_expr(right))),
            AstNode::Ternary(condition, consequent, alternate) => AstNode::Ternary(
                Box::new(self.clone_expr(condition)),
                Box::new(self.clone_expr(consequent)),
                Box::new(self.clone_expr(alternate)),
            ),
            AstNode::Unary(op, operand) => AstNode::Unary(op.clone(), Box::new(self.clone_expr(operand))),
            AstNode::AccessExpr(left, key) => AstNode::AccessExpr(Box::new(self.clone_expr(left)), Box::new(self.clone_expr(key))),
            AstNode::VecNew(elements, len, cap) => AstNode::VecNew(
                elements.iter().map(|e| Box::new(self.clone_expr(e))).collect(),
                len.as_ref().map(|e| Box::new(self.clone_expr(e))),
                cap.as_ref().map(|e| Box::new(self.clone_expr(e))),
            ),
            AstNode::ArrayNew(elements) => AstNode::ArrayNew(elements.iter().map(|e| Box::new(self.clone_expr(e))).collect()),
            AstNode::VecRepeatNew(default_element, len_element) => {
                AstNode::VecRepeatNew(Box::new(self.clone_expr(default_element)), Box::new(self.clone_expr(len_element)))
            }
            AstNode::ArrRepeatNew(default_element, len_element) => {
                AstNode::ArrRepeatNew(Box::new(self.clone_expr(default_element)), Box::new(self.clone_expr(len_element)))
            }
            AstNode::VecAccess(type_, left, index) => AstNode::VecAccess(type_.clone(), Box::new(self.clone_expr(left)), Box::new(self.clone_expr(index))),
            AstNode::EmptyCurlyNew => AstNode::EmptyCurlyNew,

            AstNode::MapNew(elements) => AstNode::MapNew(
                elements
                    .iter()
                    .map(|e| MapElement {
                        key: Box::new(self.clone_expr(&e.key)),
                        value: Box::new(self.clone_expr(&e.value)),
                    })
                    .collect(),
            ),
            AstNode::MapAccess(key_type, value_type, left, key) => AstNode::MapAccess(
                key_type.clone(),
                value_type.clone(),
                Box::new(self.clone_expr(left)),
                Box::new(self.clone_expr(key)),
            ),
            AstNode::StructNew(ident, type_, props) => AstNode::StructNew(
                ident.clone(),
                type_.clone(),
                props
                    .iter()
                    .map(|p| StructNewProperty {
                        type_: p.type_.clone(),
                        key: p.key.clone(),
                        value: Box::new(self.clone_expr(&p.value)),
                        start: p.start,
                        end: p.end,
                    })
                    .collect(),
            ),
            AstNode::StructSelect(instance, key, property) => AstNode::StructSelect(
                Box::new(self.clone_expr(instance)),
                key.clone(),
                TypeStructProperty {
                    type_: property.type_.clone(),
                    name: property.name.clone(),
                    start: property.start,
                    end: property.end,
                    value: if property.value.is_some() {
                        Some(Box::new(self.clone_expr(property.value.as_ref().unwrap())))
                    } else {
                        None
                    },
                },
            ),
            AstNode::TupleNew(elements) => AstNode::TupleNew(elements.iter().map(|e| Box::new(self.clone_expr(e))).collect()),
            AstNode::TupleDestr(elements) => AstNode::TupleDestr(elements.iter().map(|e| Box::new(self.clone_expr(e))).collect()),
            AstNode::TupleAccess(type_, left, index) => AstNode::TupleAccess(type_.clone(), Box::new(self.clone_expr(left)), *index),

            AstNode::SetNew(elements) => AstNode::SetNew(elements.iter().map(|e| Box::new(self.clone_expr(e))).collect()),
            AstNode::Call(call) => AstNode::Call(self.clone_call(call)),
            AstNode::MacroAsync(async_expr) => AstNode::MacroAsync(MacroAsyncExpr {
                closure_fn: self.deep_clone(&async_expr.closure_fn),
                closure_fn_void: self.deep_clone(&async_expr.closure_fn_void),
                origin_call: Box::new(self.clone_call(&async_expr.origin_call)),
                flag_expr: async_expr.flag_expr.as_ref().map(|e| Box::new(self.clone_expr(e))),
                return_type: async_expr.return_type.clone(),
            }),

            AstNode::FnDef(fn_def_mutex) => AstNode::FnDef(self.deep_clone(fn_def_mutex)),
            AstNode::New(type_, props, scalar_expr) => AstNode::New(
                type_.clone(),
                props
                    .iter()
                    .map(|p| StructNewProperty {
                        type_: p.type_.clone(),
                        key: p.key.clone(),
                        value: Box::new(self.clone_expr(&p.value)),
                        start: p.start,
                        end: p.end,
                    })
                    .collect(),
                if let Some(expr) = scalar_expr {
                    Some(Box::new(self.clone_expr(expr)))
                } else {
                    None
                },
            ),
            AstNode::As(type_, tag, src) => AstNode::As(
                type_.clone(),
                tag.as_ref().map(|e| Box::new(self.clone_expr(e))),
                Box::new(self.clone_expr(src)),
            ),
            AstNode::Is(type_, union_tag, src, binding) => AstNode::Is(
                type_.clone(),
                union_tag.as_ref().map(|e| Box::new(self.clone_expr(e))),
                src.as_ref().map(|e| Box::new(self.clone_expr(e))),
                binding.as_ref().map(|e| Box::new(self.clone_expr(e))),
            ),
            AstNode::Catch(try_expr, catch_err, catch_body) => AstNode::Catch(
                Box::new(self.clone_expr(try_expr)),
                Arc::new(Mutex::new(catch_err.lock().unwrap().clone())),
                self.clone_body(catch_body),
            ),
            AstNode::Match(subject, cases) => AstNode::Match(subject.as_ref().map(|s| Box::new(self.clone_expr(s))), self.clone_match_cases(cases)),

            AstNode::MacroSizeof(target_type) => AstNode::MacroSizeof(target_type.clone()),
            AstNode::MacroDefault(target_type) => AstNode::MacroDefault(target_type.clone()),
            AstNode::MacroReflectHash(type_) => AstNode::MacroReflectHash(type_.clone()),
            AstNode::MacroTypeEq(left, right) => AstNode::MacroTypeEq(left.clone(), right.clone()),

            AstNode::ArrayAccess(type_, left, index) => AstNode::ArrayAccess(type_.clone(), Box::new(self.clone_expr(left)), Box::new(self.clone_expr(index))),

            AstNode::SelectExpr(left, key, type_args) => AstNode::SelectExpr(Box::new(self.clone_expr(left)), key.clone(), type_args.clone()),
            _ => expr.node.clone(),
        };

        Expr {
            start: expr.start,
            end: expr.end,
            type_: expr.type_.clone(),
            target_type: expr.target_type.clone(),
            node,
        }
    }

    fn clone_stmt(&mut self, stmt: &Stmt) -> Stmt {
        let node = match &stmt.node {
            AstNode::Fake(expr) => AstNode::Fake(Box::new(self.clone_expr(expr))),
            AstNode::Ret(expr) => AstNode::Ret(Box::new(self.clone_expr(expr))),
            AstNode::VarDecl(var_decl) => AstNode::VarDecl(Arc::new(Mutex::new(var_decl.lock().unwrap().clone()))),
            AstNode::VarDef(var_decl, right) => AstNode::VarDef(Arc::new(Mutex::new(var_decl.lock().unwrap().clone())), Box::new(self.clone_expr(right))),
            AstNode::VarTupleDestr(elements, expr) => {
                let new_elements: Vec<Box<Expr>> = elements.iter().map(|e| Box::new(self.clone_expr(e))).collect();
                AstNode::VarTupleDestr(new_elements, Box::new(self.clone_expr(expr)))
            }
            AstNode::Assign(left, right) => AstNode::Assign(Box::new(self.clone_expr(left)), Box::new(self.clone_expr(right))),
            AstNode::If(condition, consequent, alternate) => {
                AstNode::If(Box::new(self.clone_expr(condition)), self.clone_body(consequent), self.clone_body(alternate))
            }
            AstNode::ForCond(condition, body) => AstNode::ForCond(Box::new(self.clone_expr(condition)), self.clone_body(body)),
            AstNode::ForIterator(iterate, first, second, body) => AstNode::ForIterator(
                Box::new(self.clone_expr(iterate)),
                Arc::new(Mutex::new(first.lock().unwrap().clone())),
                second.as_ref().map(|s| Arc::new(Mutex::new(s.lock().unwrap().clone()))),
                self.clone_body(body),
            ),
            AstNode::ForTradition(init, cond, update, body) => AstNode::ForTradition(
                Box::new(self.clone_stmt(init)),
                Box::new(self.clone_expr(cond)),
                Box::new(self.clone_stmt(update)),
                self.clone_body(body),
            ),
            AstNode::FnDef(fn_def_mutex) => AstNode::FnDef(self.deep_clone(fn_def_mutex)),
            AstNode::Throw(expr) => AstNode::Throw(Box::new(self.clone_expr(expr))),
            AstNode::Return(expr_opt) => AstNode::Return(expr_opt.as_ref().map(|e| Box::new(self.clone_expr(e)))),
            AstNode::Call(call) => AstNode::Call(self.clone_call(call)),
            AstNode::Continue => AstNode::Continue,
            AstNode::Break => AstNode::Break,
            AstNode::Catch(try_expr, catch_err, catch_body) => AstNode::Catch(
                Box::new(self.clone_expr(try_expr)),
                Arc::new(Mutex::new(catch_err.lock().unwrap().clone())),
                self.clone_body(catch_body),
            ),
            AstNode::Select(cases, has_default, send_count, recv_count) => {
                AstNode::Select(self.clone_select_cases(cases), *has_default, *send_count, *recv_count)
            }
            AstNode::Match(subject, cases) => AstNode::Match(subject.as_ref().map(|s| Box::new(self.clone_expr(s))), self.clone_match_cases(cases)),

            AstNode::TryCatch(try_body, catch_err, catch_body) => AstNode::TryCatch(
                self.clone_body(try_body),
                Arc::new(Mutex::new(catch_err.lock().unwrap().clone())),
                self.clone_body(catch_body),
            ),

            AstNode::Typedef(alias) => AstNode::Typedef(Arc::new(Mutex::new(alias.lock().unwrap().clone()))),
            _ => stmt.node.clone(),
        };

        Stmt {
            start: stmt.start,
            end: stmt.end,
            node,
        }
    }

    fn clone_match_cases(&mut self, cases: &Vec<MatchCase>) -> Vec<MatchCase> {
        cases
            .iter()
            .map(|case| MatchCase {
                cond_list: case.cond_list.iter().map(|e| Box::new(self.clone_expr(e))).collect(),
                is_default: case.is_default,
                handle_body: self.clone_body(&case.handle_body),
                start: case.start,
                end: case.end,
            })
            .collect()
    }

    fn clone_select_cases(&mut self, cases: &Vec<SelectCase>) -> Vec<SelectCase> {
        cases
            .iter()
            .map(|case| SelectCase {
                on_call: case.on_call.as_ref().map(|call| self.clone_call(call)),
                recv_var: case.recv_var.as_ref().map(|var| Arc::new(Mutex::new(var.lock().unwrap().clone()))),
                is_recv: case.is_recv,
                is_default: case.is_default,
                handle_body: self.clone_body(&case.handle_body),
                start: case.start,
                end: case.end,
            })
            .collect()
    }

    fn clone_call(&mut self, call: &AstCall) -> AstCall {
        AstCall {
            return_type: call.return_type.clone(),
            left: Box::new(self.clone_expr(&call.left)),
            generics_args: call.generics_args.clone(),
            args: call.args.iter().map(|e| Box::new(self.clone_expr(e))).collect(),
            spread: call.spread,
        }
    }
}

#[derive(Debug)]
pub struct Typesys<'a> {
    symbol_table: &'a mut SymbolTable,
    module: &'a mut Module,
    current_fn_mutex: Arc<Mutex<AstFnDef>>,
    worklist: Vec<Arc<Mutex<AstFnDef>>>,
    generics_args_stack: Vec<HashMap<String, Type>>,
    be_caught: usize,
    ret_target_types: Vec<Type>,
    in_for_count: usize,
    errors: Vec<AnalyzerError>,
}

impl<'a> Typesys<'a> {
    pub fn new(symbol_table: &'a mut SymbolTable, module: &'a mut Module) -> Self {
        Self {
            symbol_table,
            module,
            worklist: Vec::new(),
            generics_args_stack: Vec::new(),
            current_fn_mutex: Arc::new(Mutex::new(AstFnDef::default())),
            be_caught: 0,
            ret_target_types: Vec::new(),
            in_for_count: 0,
            errors: Vec::new(),
        }
    }

    fn type_recycle_check(&mut self, t: &Type, visited: &mut HashSet<String>) -> Option<String> {
        if t.ident.len() > 0 {
            if visited.contains(&t.ident) {
                return Some(t.ident.clone());
            }

            visited.insert(t.ident.clone());
        }

        match &t.kind {
            TypeKind::Struct(_ident, _align, properties) => {
                for property in properties {
                    let temp = self.type_recycle_check(&property.type_, visited);
                    if temp.is_some() {
                        return temp;
                    }
                }
            }
            TypeKind::Arr(_, _, element_type) => {
                return self.type_recycle_check(element_type, visited);
            }
            _ => {}
        }

        if t.kind == TypeKind::Ident && t.ident_kind != TypeIdentKind::GenericsParam {
            if let Some(symbol) = self.symbol_table.get_symbol(t.symbol_id) {
                if let SymbolKind::Type(typedef_mutex) = symbol.kind.clone() {
                    let mut typedef = typedef_mutex.lock().unwrap();

                    let mut stack_pushed = false;
                    if t.args.len() > 0 {
                        let mut args_table = HashMap::new();
                        for (index, arg) in t.args.iter().enumerate() {
                            let param = &mut typedef.params[index];

                            args_table.insert(param.ident.clone(), arg.clone());
                        }

                        self.generics_args_stack.push(args_table);
                        stack_pushed = true;
                    }

                    let temp = self.type_recycle_check(&typedef.type_expr.clone(), visited);
                    if temp.is_some() {
                        return temp;
                    }

                    if stack_pushed {
                        self.generics_args_stack.pop();
                    }
                }
            }
        }

        if t.ident.len() > 0 {
            visited.remove(&t.ident);
        }

        return None;
    }

    fn finalize_type(&mut self, t: Type, ident: String, ident_kind: TypeIdentKind, args: Vec<Type>) -> Result<Type, AnalyzerError> {
        let mut result = t.clone();
        if ident_kind != TypeIdentKind::Unknown {
            result.ident_kind = ident_kind;
            result.ident = ident;
            result.args = args;
        }

        result.status = ReductionStatus::Done;
        result.kind = Type::cross_kind_trans(&result.kind);

        // recycle_check
        let mut visited = HashSet::new();
        let found = self.type_recycle_check(&result, &mut visited);
        if found.is_some() {
            return Err(AnalyzerError {
                start: result.start,
                end: result.end,
                message: format!("recycle use type '{}'", found.unwrap()),
            });
        }

        return Ok(result);
    }

    fn reduction_ident_depth_leave(visited: &mut HashMap<String, usize>, ident: &str) {
        let depth_now = visited.get(ident).copied().unwrap_or(0);
        if depth_now <= 1 {
            visited.remove(ident);
        } else {
            visited.insert(ident.to_string(), depth_now - 1);
        }
    }

    fn combination_interface(&mut self, typedef_stmt: &mut TypedefStmt, visited: &mut HashMap<String, usize>) -> Result<(), AnalyzerError> {
        // 确保类型表达式已完成归约且是接口类型
        debug_assert!(typedef_stmt.type_expr.status == ReductionStatus::Done);

        // 获取原始接口中的方法列表
        let TypeKind::Interface(origin_elements) = &mut typedef_stmt.type_expr.kind else {
            return Err(AnalyzerError {
                start: typedef_stmt.symbol_start,
                end: typedef_stmt.symbol_end,
                message: "typedef type is not interface".to_string(),
            });
        };

        // 创建一个 HashMap 用于跟踪已存在的方法
        let mut exists = HashMap::new();

        // 将原始接口中的方法添加到 exists 中
        for element in origin_elements.clone() {
            if let TypeKind::Fn(type_fn) = &element.kind {
                exists.insert(type_fn.name.clone(), element.clone());
            }
        }

        // 合并实现的接口
        for impl_interface in &mut typedef_stmt.impl_interfaces {
            // 归约接口类型
            *impl_interface = match self.reduction_type_visited(impl_interface.clone(), visited) {
                Ok(r) => r,
                Err(_) => continue,
            };

            if let TypeKind::Interface(elements) = &impl_interface.kind {
                for element in elements {
                    if let TypeKind::Fn(type_fn) = &element.kind {
                        // 检查方法是否已存在
                        if let Some(exist_type) = exists.get(&type_fn.name) {
                            // 比较方法签名是否一致
                            if !self.type_compare(&element, exist_type) {
                                return Err(AnalyzerError {
                                    start: element.start,
                                    end: element.end,
                                    message: format!("duplicate method '{}'", type_fn.name),
                                });
                            }
                            continue;
                        }

                        // 添加新方法
                        exists.insert(type_fn.name.clone(), element.clone());
                        origin_elements.push(element.clone());
                    }
                }
            }
        }

        Ok(())
    }

    fn reduction_type_ident(&mut self, mut t: Type, visited: &mut HashMap<String, usize>) -> Result<Type, AnalyzerError> {
        let start = t.start;
        let end = t.end;

        // semantic 阶段已经找到了 alias 定义点的 symbol_Id
        if t.symbol_id == 0 {
            return Err(AnalyzerError {
                start: 0,
                end: 0,
                message: format!("typedef '{}' symbol_id not found", t.ident),
            });
        }

        // 获取符号定义
        let symbol = self.symbol_table.get_symbol(t.symbol_id).ok_or_else(|| AnalyzerError {
            start,
            end,
            message: format!("typedef '{}' not found", t.ident),
        })?;

        // 检查符号类型
        let SymbolKind::Type(typedef_mutex) = symbol.kind.clone() else {
            return Err(AnalyzerError {
                start,
                end,
                message: format!("'{}' is not a type", symbol.ident),
            });
        };

        {
            let typedef = typedef_mutex.lock().unwrap();
            if typedef.params.is_empty() {
                if !t.args.is_empty() {
                    return Err(AnalyzerError {
                        start,
                        end,
                        message: format!("typedef '{}' args mismatch", t.ident),
                    });
                }

                // 非泛型 alias 已经 reduction 完成，直接复用
                if typedef.type_expr.status == ReductionStatus::Done {
                    t.kind = typedef.type_expr.kind.clone();
                    t.status = typedef.type_expr.status;
                    return Ok(t);
                }
            }
        }

        // 通过 ident 记录 reduction 路径深度，允许深度 2，第三层中断
        let mut reduction_ident_depth_entered = false;
        if !t.ident.is_empty() {
            let reduction_depth = visited.get(&t.ident).copied().unwrap_or(0) + 1;
            visited.insert(t.ident.clone(), reduction_depth);
            reduction_ident_depth_entered = true;

            if reduction_depth >= 3 {
                Self::reduction_ident_depth_leave(visited, &t.ident);
                return Ok(t);
            }
        }

        let mut typedef = typedef_mutex.lock().unwrap();

        // 处理泛型参数
        if !typedef.params.is_empty() {
            let mut generic_typedef = typedef.clone();
            drop(typedef);

            if t.args.is_empty() {
                if reduction_ident_depth_entered {
                    Self::reduction_ident_depth_leave(visited, &t.ident);
                }

                return Err(AnalyzerError {
                    start,
                    end,
                    message: format!("typedef '{}' need params", t.ident),
                });
            }

            if t.args.len() != generic_typedef.params.len() {
                if reduction_ident_depth_entered {
                    Self::reduction_ident_depth_leave(visited, &t.ident);
                }

                return Err(AnalyzerError {
                    start,
                    end,
                    message: format!("typedef '{}' params mismatch", t.ident),
                });
            }

            let mut args_table = HashMap::new();
            let mut impl_args = Vec::new();
            for (i, undo_arg) in t.args.iter().enumerate() {
                let arg = match self.reduction_type_visited(undo_arg.clone(), visited) {
                    Ok(arg) => arg,
                    Err(e) => {
                        if reduction_ident_depth_entered {
                            Self::reduction_ident_depth_leave(visited, &t.ident);
                        }
                        return Err(e);
                    }
                };

                let param = &mut generic_typedef.params[i];
                if let Err(e) = self.generics_constrains_check(param, &arg) {
                    if reduction_ident_depth_entered {
                        Self::reduction_ident_depth_leave(visited, &t.ident);
                    }

                    return Err(AnalyzerError {
                        start,
                        end,
                        message: format!("generics constraint check failed: {}", e),
                    });
                }

                impl_args.push(arg.clone());
                args_table.insert(param.ident.clone(), arg);
            }

            self.generics_args_stack.push(args_table);
            let reduction_result: Result<Type, AnalyzerError> = (|| {
                if !generic_typedef.impl_interfaces.is_empty() {
                    if generic_typedef.is_interface {
                        debug_assert!(generic_typedef.type_expr.status == ReductionStatus::Done);
                        debug_assert!(matches!(generic_typedef.type_expr.kind, TypeKind::Interface(..)));
                        self.combination_interface(&mut generic_typedef, visited)?;
                    } else {
                        for impl_interface in &mut generic_typedef.impl_interfaces {
                            *impl_interface = self.reduction_type_visited(impl_interface.clone(), visited)?;
                        }

                        for impl_interface in &generic_typedef.impl_interfaces {
                            self.check_typedef_impl(impl_interface, t.ident.clone(), &generic_typedef)
                                .map_err(|e| AnalyzerError { start, end, message: e })?;
                        }
                    }
                }

                let right_type = self.reduction_type_visited(generic_typedef.type_expr.clone(), visited)?;
                Ok(right_type)
            })();
            self.generics_args_stack.pop();

            let right_type = match reduction_result {
                Ok(right_type) => right_type,
                Err(e) => {
                    if reduction_ident_depth_entered {
                        Self::reduction_ident_depth_leave(visited, &t.ident);
                    }
                    return Err(e);
                }
            };

            {
                let mut typedef = typedef_mutex.lock().unwrap();
                typedef.impl_interfaces = generic_typedef.impl_interfaces.clone();
            }

            t.args = impl_args;
            t.kind = right_type.kind;
            t.status = right_type.status;

            if reduction_ident_depth_entered {
                Self::reduction_ident_depth_leave(visited, &t.ident);
            }
            return Ok(t);
        }

        // interface 需要通过 ident 区分
        if typedef.is_interface {
            typedef.type_expr.ident_kind = TypeIdentKind::Interface;
            typedef.type_expr.ident = t.ident.clone();
        }

        if typedef.is_enum {
            typedef.type_expr.ident_kind = TypeIdentKind::Enum;
            typedef.type_expr.ident = t.ident.clone();
        }

        let type_expr = typedef.type_expr.clone();
        drop(typedef);

        let type_expr = match self.reduction_type_visited(type_expr, visited) {
            Ok(type_expr) => type_expr,
            Err(e) => {
                if reduction_ident_depth_entered {
                    Self::reduction_ident_depth_leave(visited, &t.ident);
                }
                return Err(e);
            }
        };

        let mut typedef = typedef_mutex.lock().unwrap();
        typedef.type_expr = type_expr;

        if !typedef.impl_interfaces.is_empty() {
            if typedef.is_interface {
                debug_assert!(typedef.type_expr.status == ReductionStatus::Done);
                debug_assert!(matches!(typedef.type_expr.kind, TypeKind::Interface(..)));
                if let Err(e) = self.combination_interface(&mut typedef, visited) {
                    if reduction_ident_depth_entered {
                        Self::reduction_ident_depth_leave(visited, &t.ident);
                    }
                    return Err(e);
                }
            } else {
                for impl_interface in &mut typedef.impl_interfaces {
                    *impl_interface = match self.reduction_type_visited(impl_interface.clone(), visited) {
                        Ok(impl_interface) => impl_interface,
                        Err(e) => {
                            if reduction_ident_depth_entered {
                                Self::reduction_ident_depth_leave(visited, &t.ident);
                            }
                            return Err(e);
                        }
                    };
                }

                for impl_interface in &typedef.impl_interfaces {
                    if let Err(e) = self.check_typedef_impl(impl_interface, t.ident.clone(), &typedef) {
                        if reduction_ident_depth_entered {
                            Self::reduction_ident_depth_leave(visited, &t.ident);
                        }
                        return Err(AnalyzerError { start, end, message: e });
                    }
                }
            }
        }

        t.kind = typedef.type_expr.kind.clone();
        t.status = typedef.type_expr.status;

        if reduction_ident_depth_entered {
            Self::reduction_ident_depth_leave(visited, &t.ident);
        }
        Ok(t)
    }

    fn reduction_complex_type(&mut self, t: Type, visited: &mut HashMap<String, usize>) -> Result<Type, AnalyzerError> {
        let mut result = t.clone();

        let kind_str = result.kind.to_string();

        match &mut result.kind {
            // 处理指针类型
            TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => {
                *value_type = Box::new(self.reduction_type_visited(*value_type.clone(), visited)?);
            }

            // 处理数组类型
            TypeKind::Arr(_, _, element_type) => {
                *element_type = Box::new(self.reduction_type_visited(*element_type.clone(), visited)?);
            }

            // 处理通道类型
            TypeKind::Chan(element_type) => {
                *element_type = Box::new(self.reduction_type_visited(*element_type.clone(), visited)?);

                result.ident_kind = TypeIdentKind::Builtin;
                result.ident = kind_str;
                result.args.push(*element_type.clone());
            }

            // 处理向量类型
            TypeKind::Vec(element_type) => {
                *element_type = Box::new(self.reduction_type_visited(*element_type.clone(), visited)?);

                result.ident_kind = TypeIdentKind::Builtin;
                result.ident = kind_str;
                result.args = vec![*element_type.clone()];
            }

            // 处理映射类型
            TypeKind::Map(key_type, value_type) => {
                *key_type = Box::new(self.reduction_type_visited(*key_type.clone(), visited)?);
                *value_type = Box::new(self.reduction_type_visited(*value_type.clone(), visited)?);

                // 检查键类型是否合法
                if !Type::is_map_key_type(&key_type.kind) {
                    return Err(AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("type '{}' not support as map key", key_type),
                    });
                }

                result.ident_kind = TypeIdentKind::Builtin;
                result.ident = kind_str;
                result.args = vec![*key_type.clone(), *value_type.clone()];
            }

            // 处理集合类型
            TypeKind::Set(element_type) => {
                *element_type = Box::new(self.reduction_type_visited(*element_type.clone(), visited)?);

                // 检查元素类型是否合法
                if !Type::is_map_key_type(&element_type.kind) {
                    return Err(AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("type '{}' not support as set element", element_type),
                    });
                }

                result.ident_kind = TypeIdentKind::Builtin;
                result.ident = kind_str;
                result.args = vec![*element_type.clone()];
            }

            // 处理元组类型
            TypeKind::Tuple(elements, _align) => {
                if elements.is_empty() {
                    return Err(AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: "tuple element empty".to_string(),
                    });
                }

                for element_type in elements.iter_mut() {
                    *element_type = self.reduction_type_visited(element_type.clone(), visited)?;
                }
            }
            TypeKind::Fn(type_fn) => {
                type_fn.return_type = self.reduction_type_visited(type_fn.return_type.clone(), visited)?;

                for formal_type in type_fn.param_types.iter_mut() {
                    *formal_type = self.reduction_type_visited(formal_type.clone(), visited)?;
                }
            }

            // 处理结构体类型
            TypeKind::Struct(_ident, _align, properties) => {
                for property in properties.iter_mut() {
                    if !property.type_.kind.is_unknown() {
                        property.type_ = self.reduction_type_visited(property.type_.clone(), visited)?;
                    }

                    if let Some(right_value) = &mut property.value {
                        match self.infer_right_expr(right_value, property.type_.clone()) {
                            Ok(right_type) => {
                                if property.type_.kind.is_unknown() {
                                    // 如果属性类型未知,则 必须能够推导其类型
                                    if !self.type_confirm(&right_type) {
                                        self.errors_push(
                                            right_value.start,
                                            right_value.end,
                                            format!("struct property '{}' type not confirmed", property.name),
                                        );
                                    }

                                    property.type_ = right_type;
                                }
                            }
                            Err(e) => {
                                self.errors_push(e.start, e.end, e.message);
                            }
                        }
                    }

                    if !self.type_confirm(&property.type_) {
                        self.errors_push(
                            property.type_.start,
                            property.type_.end,
                            format!("struct property '{}' type not confirmed", property.name),
                        );
                    }
                }
            }

            _ => {
                return Err(AnalyzerError {
                    start: 0,
                    end: 0,
                    message: "unknown type".to_string(),
                })
            }
        }

        Ok(result)
    }

    pub fn type_param_special(&mut self, t: Type, arg_table: HashMap<String, Type>, visited: &mut HashMap<String, usize>) -> Type {
        debug_assert!(t.kind == TypeKind::Ident);
        debug_assert!(t.ident_kind == TypeIdentKind::GenericsParam);

        let arg_type = arg_table.get(&t.ident).unwrap();
        return self.reduction_type_visited(arg_type.clone(), visited).unwrap();
    }

    pub fn reduction_type(&mut self, t: Type) -> Result<Type, AnalyzerError> {
        let mut visited = HashMap::new();
        self.reduction_type_visited(t, &mut visited)
    }

    fn reduction_type_visited(&mut self, mut t: Type, visited: &mut HashMap<String, usize>) -> Result<Type, AnalyzerError> {
        let ident = t.ident.clone();
        let ident_kind = t.ident_kind.clone();
        let args = t.args.clone();

        if t.err {
            return Err(AnalyzerError {
                start: 0,
                end: 0,
                message: format!("type {} already has error", t),
            });
        }

        if t.kind.is_unknown() {
            return Ok(t);
        }

        if t.status == ReductionStatus::Done {
            return self.finalize_type(t, ident, ident_kind, args);
        }

        if Type::is_ident(&t) {
            t = self.reduction_type_ident(t, visited)?;
            // 如果原来存在 t.args 则其经过了 reduction
            return self.finalize_type(t.clone(), ident, ident_kind, t.args);
        }

        if t.kind == TypeKind::Ident && t.ident_kind == TypeIdentKind::GenericsParam {
            if self.generics_args_stack.is_empty() {
                return self.finalize_type(t, ident, ident_kind, args);
            }

            let arg_table = self.generics_args_stack.last().unwrap();
            let result = self.type_param_special(t, arg_table.clone(), visited);

            return self.finalize_type(result.clone(), result.ident.clone(), result.ident_kind, result.args);
        }

        match &mut t.kind {
            TypeKind::Union(any, _, elements) => {
                if *any {
                    return self.finalize_type(t, ident, ident_kind, args);
                }

                for element in elements {
                    *element = self.reduction_type_visited(element.clone(), visited)?;
                }

                return self.finalize_type(t, ident, ident_kind, args);
            }
            TypeKind::Interface(elements) => {
                for element in elements {
                    *element = self.reduction_type_visited(element.clone(), visited)?;
                }

                return self.finalize_type(t, ident, ident_kind, args);
            }
            TypeKind::TaggedUnion(_, elements) => {
                for element in elements {
                    element.type_ = self.reduction_type_visited(element.type_.clone(), visited)?;
                }

                return self.finalize_type(t, ident, ident_kind, args);
            }
            TypeKind::Enum(element_type, properties) => {
                // reduction element type
                *element_type = Box::new(self.reduction_type_visited(*element_type.clone(), visited)?);

                // enum 只支持 integer 类型
                if !Type::is_integer(&element_type.kind) {
                    return Err(AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: format!("enum only supports integer types, got '{}'", element_type),
                    });
                }

                // 计算所有枚举成员的值
                let mut auto_value: i64 = 0;
                for prop in properties.iter_mut() {
                    if let Some(value_expr) = &mut prop.value_expr {
                        // 对表达式进行推断
                        self.infer_right_expr(value_expr, *element_type.clone())?;

                        // 目前仅支持字面量值
                        if let AstNode::Literal(_kind, value) = &value_expr.node {
                            prop.value = Some(value.clone());
                            if let Ok(v) = value.parse::<i64>() {
                                auto_value = v + 1;
                            }
                        } else {
                            return Err(AnalyzerError {
                                start: t.start,
                                end: t.end,
                                message: format!("enum member '{}' value must be a literal", prop.name),
                            });
                        }
                    } else {
                        // 没有显式值，使用自动递增值
                        prop.value = Some(auto_value.to_string());
                        auto_value += 1;
                    }
                }

                return self.finalize_type(t, ident, ident_kind, args);
            }
            _ => {
                if Type::is_origin_type(&t.kind) {
                    let mut result = t.clone();

                    result.status = ReductionStatus::Done;
                    result.ident = t.kind.to_string(); // 如果 origin ident 存在，则会被覆盖
                    result.ident_kind = TypeIdentKind::Builtin; // 如果 origin ident_kind 存在，则会被覆盖
                    result.args = vec![];

                    return self.finalize_type(result, ident, ident_kind, args);
                }

                if Type::is_complex_type(&t.kind) {
                    let result = self.reduction_complex_type(t, visited)?;
                    return self.finalize_type(result, ident, ident_kind, args);
                }

                return Err(AnalyzerError {
                    start: 0,
                    end: 0,
                    message: "unknown type".to_string(),
                });
            }
        }
    }

    /**
     * integer literal 可以转换为 float 类型，不做检查
     * float literal 可以转换为 integer 类型，不做检查
     * float literal 可以转换为 float 类型，不做检查
     *
     * integer literal 之间的转换需要进行 fit 检查
     */
    fn literal_as_check(&mut self, literal_kind: &mut TypeKind, literal_value: &mut String, target_kind: TypeKind) -> Result<(), String> {
        let literal_kind = Type::cross_kind_trans(literal_kind);
        let target_kind = Type::cross_kind_trans(&target_kind);

        // 如果不是整数类型之间的转换，直接返回 true
        if !Type::is_integer(&literal_kind) || !Type::is_integer(&target_kind) {
            return Ok(());
        }

        let (literal_value, is_negative) = if literal_value.starts_with('-') {
            (literal_value[1..].to_string(), true)
        } else {
            (literal_value.clone(), false)
        };

        // 统一处理数字转换
        let i = if literal_value.starts_with("0x") {
            i64::from_str_radix(&literal_value[2..], 16)
        } else if literal_value.starts_with("0b") {
            i64::from_str_radix(&literal_value[2..], 2)
        } else if literal_value.starts_with("0o") {
            i64::from_str_radix(&literal_value[2..], 8)
        } else {
            literal_value.parse::<i64>()
        }
        .map_err(|e| e.to_string())?;

        let i = if is_negative { -i } else { i };

        if self.integer_range_check(&target_kind, i) {
            return Ok(());
        }

        return Err(format!("literal {} out of type '{}' range", literal_value, literal_kind.to_string()));
    }

    pub fn infer_as_expr(&mut self, as_expr: &mut Box<Expr>) -> Result<Type, AnalyzerError> {
        // 先还原目标类型
        let AstNode::As(target_type, union_tag, src) = &mut as_expr.node else {
            unreachable!()
        };

        // 推导源表达式类型, 如果存在错误则停止后续比较, 直接返回错误
        let src_type = self.infer_expr(src, Type::default(), Type::default())?;
        if src_type.kind.is_unknown() {
            return Err(AnalyzerError {
                start: 0,
                end: 0,
                message: "unknown as source type".to_string(),
            });
        }

        src.type_ = src_type.clone();

        // as union tag: 处理 tagged union 的类型断言
        if let Some(ut) = union_tag {
            if !matches!(src.type_.kind, TypeKind::TaggedUnion(..)) {
                return Err(AnalyzerError {
                    start: as_expr.start,
                    end: as_expr.end,
                    message: "unexpected as expr, expected tagged union".to_string(),
                });
            }
            self.infer_tagged_union_element(ut, src.type_.clone())?;

            // 获取 element 的类型作为 target_type
            if let AstNode::TaggedUnionElement(_, _, Some(element)) = &ut.node {
                *target_type = element.type_.clone();
                return Ok(element.type_.clone());
            }
        }

        *target_type = self.reduction_type(target_type.clone())?;

        // anyptr 可以 as 为任意类型
        if matches!(src.type_.kind, TypeKind::Anyptr) && !Type::is_float(&target_type.kind) {
            return Ok(target_type.clone());
        }

        // 除了 float 任意类型都可以 as anyptr
        if !Type::is_float(&src.type_.kind) && matches!(target_type.kind, TypeKind::Anyptr) {
            return Ok(target_type.clone());
        }

        // union/nay 可以 as 为任意类型
        // 处理联合类型转换
        if let TypeKind::Union(any, _, elements) = &src_type.kind {
            if matches!(target_type.kind, TypeKind::Union(..)) {
                return Err(AnalyzerError {
                    start: as_expr.start,
                    end: as_expr.end,
                    message: "union to union type is not supported".to_string(),
                });
            }

            if !self.union_type_contains(&(*any, elements.clone()), &target_type) {
                return Err(AnalyzerError {
                    start: as_expr.start,
                    end: as_expr.end,
                    message: format!("type {} not contains in union type", target_type),
                });
            }

            return Ok(target_type.clone());
        }

        // 处理接口类型转换
        if let TypeKind::Interface(_) = &src_type.kind {
            // interface_type = src_type
            let temp_target_type = match &target_type.kind {
                TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => *value_type.clone(),
                _ => target_type.clone(),
            };

            if temp_target_type.ident.is_empty() || temp_target_type.ident_kind != TypeIdentKind::Def {
                return Err(AnalyzerError {
                    start: as_expr.start,
                    end: as_expr.end,
                    message: format!("type {} not impl interface", src_type),
                });
            }

            // get symbol from symbol table
            let symbol = match self.symbol_table.find_global_symbol(&temp_target_type.ident) {
                Some(s) => s,
                None => {
                    return Err(AnalyzerError {
                        start: 0,
                        end: 0,
                        message: format!("type '{}' not found", temp_target_type.ident),
                    });
                }
            };

            if let SymbolKind::Type(typedef_mutex) = symbol.kind.clone() {
                let typedef = typedef_mutex.lock().unwrap();
                let found = self.check_impl_interface_contains(&typedef, &src_type);

                // 禁止制鸭子类型
                if !found {
                    return Err(AnalyzerError {
                        start: as_expr.start,
                        end: as_expr.end,
                        message: format!("type '{}' not impl '{}' interface", temp_target_type.ident, src_type),
                    });
                }

                self.check_typedef_impl(&src_type, temp_target_type.ident.clone(), &typedef)
                    .map_err(|e| AnalyzerError {
                        start: as_expr.start,
                        end: as_expr.end,
                        message: e,
                    })?;
            } else {
                unreachable!();
            }

            return Ok(target_type.clone());
        }

        // string -> list u8
        if matches!(src_type.kind, TypeKind::String) && Type::is_list_u8(&target_type.kind) {
            return Ok(target_type.clone());
        }

        // list u8 -> string
        if Type::is_list_u8(&src_type.kind) && matches!(target_type.kind, TypeKind::String) {
            return Ok(target_type.clone());
        }

        // number 之间可以相互进行类型转换, 但是需要进行 literal check
        if Type::is_number(&src_type.kind) && Type::is_number(&target_type.kind) {
            if let AstNode::Literal(literal_kind, literal_value) = &mut src.node {
                self.literal_as_check(&mut literal_kind.clone(), &mut literal_value.clone(), target_type.kind.clone())
                    .map_err(|e| AnalyzerError {
                        start: as_expr.start,
                        end: as_expr.end,
                        message: e,
                    })?;
            }

            return Ok(target_type.clone());
        }

        // enum origin cast
        if let TypeKind::Enum(element_type, _) = &src_type.kind {
            if self.type_compare(element_type, &target_type) {
                return Ok(target_type.clone());
            }
        }

        // 处理 typedef ident 类型转换
        if target_type.ident_kind == TypeIdentKind::Def {
            if self.type_compare_no_ident(target_type.clone(), src.type_.clone()) {
                src.type_.ident = target_type.ident.clone();
                src.type_.ident_kind = target_type.ident_kind.clone();
                src.type_.args = target_type.args.clone();
                return Ok(target_type.clone());
            }
        }

        if src.type_.ident_kind == TypeIdentKind::Def && target_type.ident_kind == TypeIdentKind::Builtin {
            if self.type_compare_no_ident(target_type.clone(), src.type_.clone()) {
                src.type_.ident = target_type.ident.clone();
                src.type_.ident_kind = target_type.ident_kind.clone();
                src.type_.args = target_type.args.clone();
                return Ok(target_type.clone());
            }
        }

        // 检查目标类型是否可以进行类型转换
        return Err(AnalyzerError {
            start: as_expr.start,
            end: as_expr.end,
            message: format!("cannot casting to '{}'", target_type),
        });
    }

    pub fn infer_match(
        &mut self,
        subject: &mut Option<Box<Expr>>,
        cases: &mut Vec<MatchCase>,
        target_type: Type,
        start: usize,
        end: usize,
    ) -> Result<Type, AnalyzerError> {
        // 默认 subject_type 为 bool 类型(用于无 subject 的情况)
        let mut subject_type = Type::new(TypeKind::Bool);

        // 如果存在 subject,推导其类型, 如果推倒错误则停止后续推倒，因为无法识别具体的类型
        if let Some(subject_expr) = subject {
            subject_type = self.infer_right_expr(subject_expr, Type::default())?;

            // 确保 subject 类型已确定
            if !self.type_confirm(&subject_type) {
                return Err(AnalyzerError {
                    start: subject_expr.start,
                    end: subject_expr.end,
                    message: "match subject type not confirm".to_string(),
                });
            }
        }

        // 将目标类型加入 break_target_types 栈
        self.ret_target_types.push(target_type.clone());

        // 用于跟踪联合类型和 enum 的匹配情况
        let mut exhaustive_table: HashMap<String, bool> = HashMap::new();
        let mut has_default = false;

        // 遍历所有 case
        for case in cases {
            if case.is_default {
                has_default = true;
            } else {
                // 处理每个条件表达式
                for cond_expr in case.cond_list.iter_mut() {
                    // 对于联合类型和 tagged union,只能使用 is 匹配 (src=None means match-is)
                    if matches!(subject_type.kind, TypeKind::Union(..) | TypeKind::TaggedUnion(..)) {
                        if !matches!(cond_expr.node, AstNode::Is(_, _, None, _)) {
                            return Err(AnalyzerError {
                                start: cond_expr.start,
                                end: cond_expr.end,
                                message: "match 'union' only support 'is' assert".to_string(),
                            });
                        }
                    }

                    // 处理 is 类型匹配 (src=None means match-is)
                    if let AstNode::Is(_target_type, union_tag, None, _binding) = &mut cond_expr.node {
                        if !matches!(subject_type.kind, TypeKind::Union(..) | TypeKind::Interface(..) | TypeKind::TaggedUnion(..)) {
                            return Err(AnalyzerError {
                                start: cond_expr.start,
                                end: cond_expr.end,
                                message: format!("{} cannot use 'is' operator", subject_type),
                            });
                        }

                        // 处理 tagged union 的 is 匹配
                        if matches!(subject_type.kind, TypeKind::TaggedUnion(..)) {
                            let Some(union_tag_expr) = union_tag else {
                                return Err(AnalyzerError {
                                    start: cond_expr.start,
                                    end: cond_expr.end,
                                    message: "tagged union match requires union tag".to_string(),
                                });
                            };

                            // 推断 tagged union element
                            self.infer_tagged_union_element(union_tag_expr, subject_type.clone())?;

                            // 记录 tagged name
                            if let AstNode::TaggedUnionElement(_, tagged_name, _) = &union_tag_expr.node {
                                exhaustive_table.insert(tagged_name.clone(), true);
                            }

                            cond_expr.type_ = Type::new(TypeKind::Bool);
                        } else {
                            // 处理普通 union/interface 的 is 匹配
                            let cond_type = self.infer_right_expr(cond_expr, Type::default())?;
                            debug_assert!(matches!(cond_type.kind, TypeKind::Bool));

                            if let AstNode::Is(target_type, _, None, _) = &cond_expr.node {
                                // 记录已匹配的类型, 最终可以判断 match 是否匹配了所有分支
                                exhaustive_table.insert(target_type.hash().to_string(), true);
                            }
                        }
                    } else if matches!(subject_type.kind, TypeKind::Enum(..)) {
                        // enum 值匹配
                        if let AstNode::SelectExpr(_, key, _) = &cond_expr.node {
                            exhaustive_table.insert(key.clone(), true);
                        }

                        self.infer_right_expr(cond_expr, subject_type.clone())?;
                    } else {
                        // 普通值匹配,推导条件表达式类型
                        self.infer_right_expr(cond_expr, subject_type.clone())?;
                    }
                }
            }

            // 推导 case 处理体
            self.infer_body(&mut case.handle_body);
        }

        // 检查 default case
        if !has_default {
            // 对于非 any 的联合类型,检查是否所有可能的类型都已匹配
            if let TypeKind::Union(any, _, elements) = &subject_type.kind {
                if !any {
                    for element_type in elements {
                        if !exhaustive_table.contains_key(&element_type.hash().to_string()) {
                            return Err(AnalyzerError {
                                start,
                                end,
                                message: format!(
                                    "match expression lacks a default case '_' and union element type lacks, for example 'is {}'",
                                    element_type
                                ),
                            });
                        }
                    }
                } else {
                    return Err(AnalyzerError {
                        start,
                        end,
                        message: "match expression lacks a default case '_'".to_string(),
                    });
                }
            } else if let TypeKind::TaggedUnion(_, elements) = &subject_type.kind {
                // tagged union 穷尽检查
                for element in elements {
                    if !exhaustive_table.contains_key(&element.tag) {
                        return Err(AnalyzerError {
                            start,
                            end,
                            message: format!(
                                "match expression lacks a default case '_' and tagged union element lacks, for example 'is {}'",
                                element.tag
                            ),
                        });
                    }
                }
            } else if let TypeKind::Enum(_, properties) = &subject_type.kind {
                // enum 穷尽检查 - 使用 name 作为 key
                for prop in properties {
                    if !exhaustive_table.contains_key(&prop.name) {
                        return Err(AnalyzerError {
                            start,
                            end,
                            message: format!("match expression lacks a default case '_' and enum value lacks, for example '{}'", prop.name),
                        });
                    }
                }
            } else {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: "match expression lacks a default case '_'".to_string(),
                });
            }
        }

        // 弹出目标类型
        return Ok(self.ret_target_types.pop().unwrap());
    }

    pub fn infer_struct_properties(
        &mut self,
        type_properties: &mut Vec<TypeStructProperty>,
        properties: &mut Vec<StructNewProperty>,
        start: usize,
        end: usize,
    ) -> Result<Vec<StructNewProperty>, AnalyzerError> {
        // 用于跟踪已经处理过的属性
        let mut exists = HashMap::new();

        // 处理显式指定的属性
        for property in properties.iter_mut() {
            // 在类型定义中查找对应的属性
            let expect_property = type_properties.iter().find(|p| p.name == property.key).ok_or_else(|| AnalyzerError {
                start: property.start,
                end: property.end,
                message: format!("not found property '{}'", property.key),
            })?;

            exists.insert(property.key.clone(), true);

            // 推导属性值的类型
            if let Err(e) = self.infer_right_expr(&mut property.value, expect_property.type_.clone()) {
                self.errors_push(e.start, e.end, e.message);
            }

            // 冗余属性类型(用于计算size)
            property.type_ = expect_property.type_.clone();
        }

        // 处理默认值
        let mut result = properties.clone();

        // 遍历类型定义中的所有属性
        for type_prop in type_properties.iter() {
            // 如果属性已经被显式指定或没有默认值,则跳过
            if exists.contains_key(&type_prop.name) || type_prop.value.is_none() {
                continue;
            }

            exists.insert(type_prop.name.clone(), true);

            // 添加默认值属性
            result.push(StructNewProperty {
                type_: type_prop.type_.clone(),
                key: type_prop.name.clone(),
                value: type_prop.value.clone().unwrap(),
                start: type_prop.start,
                end: type_prop.end,
            });
        }

        // 检查所有必需的属性是否都已赋值
        for type_prop in type_properties.iter() {
            if exists.contains_key(&type_prop.name) {
                continue;
            }

            // 检查是否是必须赋值的类型
            if Type::must_assign_value(&type_prop.type_.kind) {
                return Err(AnalyzerError {
                    start: start,
                    end: end,
                    message: format!("struct field '{}' must be assigned default value", type_prop.name),
                });
            }
        }

        Ok(result)
    }

    pub fn infer_binary(&mut self, op: ExprOp, left: &mut Box<Expr>, right: &mut Box<Expr>, _infer_target_type: Type) -> Result<Type, AnalyzerError> {
        debug_assert!(!matches!(_infer_target_type.kind, TypeKind::Union(..)));

        let left_target_type = if op.is_arithmetic() {
            _infer_target_type
        } else {
            Type::new(TypeKind::Unknown)
        };

        // 先推导左操作数的类型
        let left_type = self.infer_right_expr(left, left_target_type)?;
        if left_type.kind.is_unknown() {
            return Err(AnalyzerError {
                start: 0,
                end: 0,
                message: "unknown binary expr left type".to_string(),
            });
        }

        let right_target_type = if let TypeKind::Union(..) = left_type.kind.clone() {
            Type::new(TypeKind::Unknown)
        } else {
            left_type.clone()
        };

        // 推导右操作数的类型
        let right_type = self.infer_right_expr(right, right_target_type)?;

        // 检查左右操作数类型是否一致
        if !self.type_compare(&left_type, &right_type) {
            return Err(AnalyzerError {
                start: left.start,
                end: right.end,
                message: format!("binary type inconsistency: left is '{}', right is '{}'", left_type, right_type),
            });
        }

        // 处理数值类型运算
        if Type::is_number(&left_type.kind) {
            // 检查右操作数也必须是数值类型
            if !Type::is_number(&right_type.kind) {
                return Err(AnalyzerError {
                    start: right.start,
                    end: right.end,
                    message: format!(
                        "binary operator '{}' only support number operand, actual '{} {} {}'",
                        op, left_type, op, right_type
                    ),
                });
            }
        }

        // 处理字符串类型运算
        if matches!(left_type.kind, TypeKind::String) {
            // 检查右操作数也必须是字符串类型
            if !matches!(right_type.kind, TypeKind::String) {
                return Err(AnalyzerError {
                    start: right.start,
                    end: right.end,
                    message: format!(
                        "binary operator '{}' only support string operand, actual '{} {} {}'",
                        op, left_type, op, right_type
                    ),
                });
            }
        }

        if op.is_bool() {
            if !matches!(left_type.kind, TypeKind::Bool) || !matches!(right_type.kind, TypeKind::Bool) {
                return Err(AnalyzerError {
                    start: left.start,
                    end: right.end,
                    message: format!(
                        "binary operator '{}' only support bool operand, actual '{} {} {}'",
                        op, left_type, op, right_type
                    ),
                });
            }
        }

        // 处理位运算操作符
        if op.is_integer() {
            // 检查操作数必须是整数类型
            if !Type::is_integer(&left_type.kind) || !Type::is_integer(&right_type.kind) {
                return Err(AnalyzerError {
                    start: left.start,
                    end: right.end,
                    message: format!("binary operator '{}' only integer operand", op),
                });
            }
        }

        // 返回结果类型
        if op.is_arithmetic() {
            // 算术运算返回左操作数类型
            Ok(left_type)
        } else if op.is_logic() {
            // 逻辑运算返回布尔类型
            Ok(Type::new(TypeKind::Bool))
        } else {
            // 未知运算符
            Err(AnalyzerError {
                start: 0,
                end: 0,
                message: format!("unknown operator '{}'", op),
            })
        }
    }

    pub fn infer_ternary(
        &mut self,
        condition: &mut Box<Expr>,
        consequent: &mut Box<Expr>,
        alternate: &mut Box<Expr>,
        infer_target_type: Type,
    ) -> Result<Type, AnalyzerError> {
        // Infer condition type - must be bool, ptr, or nullable type (truthy/falsy)
        let cond_type = self.infer_right_expr(condition, Type::new(TypeKind::Bool))?;

        // Check condition is valid truthy/falsy type
        let is_valid_cond = matches!(cond_type.kind, TypeKind::Bool)
            || matches!(cond_type.kind, TypeKind::Ptr(..))
            || matches!(cond_type.kind, TypeKind::Anyptr)
            || (matches!(cond_type.kind, TypeKind::Union(_, nullable, _) if nullable));

        if !is_valid_cond {
            return Err(AnalyzerError {
                start: condition.start,
                end: condition.end,
                message: format!("ternary condition must be bool, pointer, or nullable type, actual '{}'", cond_type),
            });
        }

        // Infer consequent expression with target type
        let consequent_type = self.infer_right_expr(consequent, infer_target_type.clone())?;

        // Infer alternate expression with consequent type as target
        let alternate_type = self.infer_right_expr(alternate, consequent_type.clone())?;

        // Check that branches have compatible types
        if !self.type_compare(&consequent_type, &alternate_type) {
            return Err(AnalyzerError {
                start: consequent.start,
                end: alternate.end,
                message: format!("ternary branches must have compatible types: '{}' vs '{}'", consequent_type, alternate_type),
            });
        }

        Ok(consequent_type)
    }

    pub fn infer_unary(&mut self, op: ExprOp, operand: &mut Box<Expr>, target_type: Type) -> Result<Type, AnalyzerError> {
        if target_type.kind == TypeKind::Void {
            return Err(AnalyzerError {
                start: operand.start,
                end: operand.end,
                message: format!("unary operator '{}' cannot use void as target type", op),
            });
        }

        // 处理逻辑非运算符
        if op == ExprOp::Not {
            // 对任何类型都可以进行布尔转换
            return Ok(self.infer_right_expr(operand, Type::new(TypeKind::Bool))?);
        }

        let operand_target_type = if op == ExprOp::Neg { target_type } else { Type::default() };

        // 获取操作数的类型
        let operand_type = self.infer_right_expr(operand, operand_target_type)?;

        // 处理负号运算符
        if op == ExprOp::Neg && !Type::is_number(&operand_type.kind) {
            return Err(AnalyzerError {
                start: operand.start,
                end: operand.end,
                message: format!("neg(-) must use in number, actual '{}'", operand_type),
            });
        }

        // 处理取地址运算符 &
        if op == ExprOp::La {
            // 检查是否是字面量或函数调用
            if matches!(operand.node, AstNode::Literal(..) | AstNode::Call(..)) {
                return Err(AnalyzerError {
                    start: operand.start,
                    end: operand.end,
                    message: "cannot load address of an literal or call".to_string(),
                });
            }

            return Ok(Type::ptr_of(operand_type));
        }

        // 处理解引用运算符 *
        if op == ExprOp::Ia {
            // 检查是否是指针类型
            match operand_type.kind {
                TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => {
                    return Ok(*value_type);
                }
                _ => {
                    return Err(AnalyzerError {
                        start: operand.start,
                        end: operand.end,
                        message: format!("cannot dereference non-pointer type '{}'", operand_type),
                    });
                }
            }
        }

        // 其他情况直接返回操作数类型
        Ok(operand_type)
    }

    pub fn infer_ident(&mut self, ident: &mut String, symbol_id: &mut NodeId, start: usize, end: usize) -> Result<Type, AnalyzerError> {
        if *symbol_id == 0 {
            return Err(AnalyzerError {
                start: 0,
                end: 0,
                message: format!("ident '{}' symbol_id is none", ident),
            });
        };

        let symbol = self.symbol_table.get_symbol(*symbol_id).unwrap();

        let mut symbol_kind = symbol.kind.clone();

        // 判断符号是否是 local symbol
        if symbol.is_local {
            if let Some(hash) = self.current_fn_mutex.lock().unwrap().generics_args_hash {
                let new_ident = format_generics_ident(ident.clone(), hash);

                if let Some(new_symbol_id) = symbol.generics_id_map.get(&new_ident) {
                    *ident = new_ident; // rewrite to generics ident
                    *symbol_id = *new_symbol_id;

                    symbol_kind = self.symbol_table.get_symbol(*symbol_id).unwrap().kind.clone();
                } else {
                    return Err(AnalyzerError {
                        start: 0,
                        end: 0,
                        message: format!("generics symbol rewrite failed, new ident '{}' not found", new_ident),
                    });
                }
            }
        }

        match symbol_kind {
            SymbolKind::Var(var_decl) => {
                let mut var_decl = var_decl.lock().unwrap();
                var_decl.type_ = self.reduction_type(var_decl.type_.clone())?;

                if var_decl.type_.kind.is_unknown() {
                    return Err(AnalyzerError {
                        start: 0,
                        end: 0,
                        message: "unknown type".to_string(),
                    });
                }
                debug_assert!(var_decl.type_.kind.is_exist());

                return Ok(var_decl.type_.clone());
            }
            SymbolKind::Fn(fndef_mutex) => {
                {
                    // 泛型 fn 不能直接作为 ident 使用
                    let fndef = fndef_mutex.lock().unwrap();
                    if fndef.is_generics {
                        return Err(AnalyzerError {
                            start,
                            end,
                            message: format!("generic fn `{}` cannot be passed as ident", fndef.fn_name),
                        });
                    }
                }

                return self.infer_fn_decl(fndef_mutex.clone(), Type::default());
            }
            _ => {
                return Err(AnalyzerError {
                    start: start,
                    end: end,
                    message: "symbol of 'type' cannot be used as an identity".to_string(),
                });
            }
        }
    }

    pub fn infer_vec_new(&mut self, expr: &mut Box<Expr>, infer_target_type: Type, literal_refer: Type) -> Result<Type, AnalyzerError> {
        let AstNode::VecNew(elements, _, _) = &mut expr.node else { unreachable!() };

        // 如果目标类型是数组，则将 VecNew 重写为 ArrayNew
        if let TypeKind::Arr(_, length, element_type) = &infer_target_type.kind {
            // 重写表达式节点为 ArrayNew
            expr.node = AstNode::ArrayNew(elements.clone());
            let AstNode::ArrayNew(elements) = &mut expr.node else { unreachable!() };

            let mut result = Type::undo_new(TypeKind::Arr(Box::new(Expr::default()), length.clone(), element_type.clone()));
            result.ident = infer_target_type.ident.clone();
            result.ident_kind = infer_target_type.ident_kind.clone();
            result.args = infer_target_type.args.clone();

            // 如果数组为空，直接返回目标类型
            if elements.is_empty() {
                return self.reduction_type(result);
            }

            // 对所有元素进行类型推导
            for element in elements {
                if let Err(e) = self.infer_right_expr(element, *element_type.clone()) {
                    self.errors_push(e.start, e.end, e.message);
                }
            }

            return self.reduction_type(result);
        }

        // 处理向量类型
        let mut element_type = Type::default(); // TYPE_UNKNOWN

        // 如果目标类型是向量，使用其元素类型
        if let TypeKind::Vec(target_element_type) = &infer_target_type.kind {
            element_type = *target_element_type.clone();
        } else if Type::is_any(&literal_refer.kind) {
            element_type = literal_refer.clone();
        }

        // 如果向量为空，必须能从目标类型确定元素类型
        if elements.is_empty() {
            if !self.type_confirm(&element_type) {
                return Err(AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: "vec element type not confirm".to_string(),
                });
            }
            let mut result = Type::undo_new(TypeKind::Vec(Box::new(element_type)));
            if infer_target_type.kind.is_exist() {
                result.ident = infer_target_type.ident.clone();
                result.ident_kind = infer_target_type.ident_kind.clone();
                result.args = infer_target_type.args.clone();
            }
            return self.reduction_type(result);
        }

        // 如果元素类型未知，使用第一个元素的类型进行推导
        if element_type.kind.is_unknown() {
            element_type = self.infer_right_expr(&mut elements[0], Type::default())?;
        }

        // 对所有元素进行类型推导和检查
        for element in elements.iter_mut() {
            if let Err(e) = self.infer_right_expr(element, element_type.clone()) {
                self.errors_push(e.start, e.end, e.message);
            }
        }
        let mut result = Type::undo_new(TypeKind::Vec(Box::new(element_type)));
        if infer_target_type.kind.is_exist() {
            result.ident = infer_target_type.ident.clone();
            result.ident_kind = infer_target_type.ident_kind.clone();
            result.args = infer_target_type.args.clone();
        }

        // 返回最终的向量类型
        let result = self.reduction_type(result);
        return result;
    }

    pub fn infer_map_new(
        &mut self,
        elements: &mut Vec<MapElement>,
        infer_target_type: Type,
        literal_type: Type,
        start: usize,
        end: usize,
    ) -> Result<Type, AnalyzerError> {
        // 创建一个新的 Map 类型，初始化 key 和 value 类型为未知
        let mut key_type = Type::default(); // TYPE_UNKNOWN
        let mut value_type = Type::default(); // TYPE_UNKNOWN

        // 如果有目标类型且是Map类型，使用目标类型的key和value类型
        if let TypeKind::Map(target_key_type, target_value_type) = &infer_target_type.kind {
            key_type = *target_key_type.clone();
            value_type = *target_value_type.clone();
        } else if Type::is_any(&literal_type.kind) {
            key_type = Type::new(TypeKind::String);
            value_type = literal_type.clone();
        }

        // 如果 Map 为空，必须能从目标类型确定 key 和 value 类型
        if elements.is_empty() {
            if !self.type_confirm(&key_type) {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: "map key type not confirm".to_string(),
                });
            }
            if !self.type_confirm(&value_type) {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: "map value type not confirm".to_string(),
                });
            }
            return self.reduction_type(Type::undo_new(TypeKind::Map(Box::new(key_type), Box::new(value_type))));
        }

        // 如果类型未知，使用第一个元素的类型进行推导
        if key_type.kind.is_unknown() {
            key_type = self.infer_right_expr(&mut elements[0].key, Type::default())?;
            value_type = self.infer_right_expr(&mut elements[0].value, Type::default())?;
        }

        // 对所有元素进行类型推导和检查
        for element in elements.iter_mut() {
            if let Err(e) = self.infer_right_expr(&mut element.key, key_type.clone()) {
                self.errors_push(e.start, e.end, e.message);
            }

            if let Err(e) = self.infer_right_expr(&mut element.value, value_type.clone()) {
                self.errors_push(e.start, e.end, e.message);
            }
        }
        let mut result = Type::undo_new(TypeKind::Map(Box::new(key_type), Box::new(value_type)));

        if infer_target_type.kind.is_exist() {
            result.ident = infer_target_type.ident.clone();
            result.ident_kind = infer_target_type.ident_kind.clone();
            result.args = infer_target_type.args.clone();
        }

        // 返回最终的Map类型
        self.reduction_type(result)
    }

    pub fn infer_set_new(&mut self, elements: &mut Vec<Box<Expr>>, infer_target_type: Type, start: usize, end: usize) -> Result<Type, AnalyzerError> {
        // 创建一个新的 Set 类型
        let mut element_type = Type::default(); // TYPE_UNKNOWN

        // 如果有目标类型且是 Set 类型，使用目标类型的元素类型
        if let TypeKind::Set(target_element_type) = &infer_target_type.kind {
            element_type = *target_element_type.clone();
        }

        // 如果集合为空，必须能从目标类型确定元素类型
        if elements.is_empty() {
            if !self.type_confirm(&element_type) {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: "empty set element type not confirm".to_string(),
                });
            }
            return self.reduction_type(Type::undo_new(TypeKind::Set(Box::new(element_type))));
        }

        // 如果元素类型未知，使用第一个元素的类型进行推导
        if element_type.kind.is_unknown() {
            element_type = self.infer_right_expr(&mut elements[0], Type::default())?;
        }

        // 对所有元素进行类型推导和检查
        for element in elements.iter_mut() {
            if let Err(e) = self.infer_right_expr(element, element_type.clone()) {
                self.errors_push(e.start, e.end, e.message);
            }
        }

        // 返回最终的 set 类型
        let mut result = Type::undo_new(TypeKind::Set(Box::new(element_type)));
        if infer_target_type.kind.is_exist() {
            result.ident = infer_target_type.ident.clone();
            result.ident_kind = infer_target_type.ident_kind.clone();
            result.args = infer_target_type.args.clone();
        }

        self.reduction_type(result)
    }

    pub fn infer_tuple_new(&mut self, elements: &mut Vec<Box<Expr>>, infer_target_type: Type, start: usize, end: usize) -> Result<Type, AnalyzerError> {
        // 检查元组元素不能为空
        if elements.is_empty() {
            return Err(AnalyzerError {
                start,
                end,
                message: "tuple elements empty".to_string(),
            });
        }

        // 收集所有元素的类型
        let mut element_types = Vec::new();
        for (i, expr) in elements.iter_mut().enumerate() {
            let element_target_type = if let TypeKind::Tuple(target_element_types, _) = &infer_target_type.kind {
                target_element_types[i].clone()
            } else {
                Type::default()
            };

            // 推导每个元素的类型
            let expr_type = self.infer_right_expr(expr, element_target_type)?;

            // 检查元素类型是否已确定
            if !self.type_confirm(&expr_type) {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: "tuple element type cannot be confirmed".to_string(),
                });
            }

            element_types.push(expr_type);
        }

        // 创建并返回元组类型
        let mut result = Type::undo_new(TypeKind::Tuple(element_types, 0));
        if infer_target_type.kind.is_exist() {
            result.ident = infer_target_type.ident.clone();
            result.ident_kind = infer_target_type.ident_kind.clone();
            result.args = infer_target_type.args.clone();
        }

        self.reduction_type(result) // 0 是对齐字节数，这里暂时使用0
    }

    pub fn infer_vec_repeat_new(&mut self, expr: &mut Box<Expr>, infer_target_type: Type) -> Result<Type, AnalyzerError> {
        let AstNode::VecRepeatNew(default_element, length_expr) = &mut expr.node.clone() else {
            unreachable!()
        };

        // 如果目标类型是数组，将表达式重写为数组重复初始化
        if let TypeKind::Arr(_, _, element_type) = &infer_target_type.kind {
            // 重写为数组重复初始化节点
            expr.node = AstNode::ArrRepeatNew(default_element.clone(), length_expr.clone());

            // 检查默认值类型
            self.infer_right_expr(default_element, *element_type.clone())?;

            // 检查长度表达式
            self.infer_right_expr(length_expr, Type::new(TypeKind::Int))?;

            // 长度必须是整数字面量
            let length = if let AstNode::Literal(kind, value) = &length_expr.node {
                if !Type::is_integer(kind) {
                    return Err(AnalyzerError {
                        start: length_expr.start,
                        end: length_expr.end,
                        message: "array length must be integer literal".to_string(),
                    });
                }
                value.parse::<i64>().map_err(|_| AnalyzerError {
                    start: length_expr.start,
                    end: length_expr.end,
                    message: "invalid array length".to_string(),
                })?
            } else {
                return Err(AnalyzerError {
                    start: length_expr.start,
                    end: length_expr.end,
                    message: "array length must be constant".to_string(),
                });
            };

            // 检查长度是否大于0
            if length <= 0 {
                return Err(AnalyzerError {
                    start: length_expr.start,
                    end: length_expr.end,
                    message: "array length must be greater than 0".to_string(),
                });
            }

            let result = Type::undo_new(TypeKind::Arr(Box::new(Expr::default()), length as u64, element_type.clone()));

            return self.reduction_type(result);
        }

        // 处理向量类型
        let mut element_type = Type::unknown();
        if let TypeKind::Vec(target_element_type) = &infer_target_type.kind {
            element_type = *target_element_type.clone();
        }

        // 推导默认值类型
        element_type = self.infer_right_expr(default_element, element_type)?;
        let result = Type::undo_new(TypeKind::Vec(Box::new(element_type)));

        return self.reduction_type(result);
    }

    pub fn infer_access_expr(&mut self, expr: &mut Box<Expr>) -> Result<Type, AnalyzerError> {
        let AstNode::AccessExpr(left, key) = &mut expr.node else { unreachable!() };

        // 推导左侧表达式的类型
        let mut left_type = self.infer_right_expr(left, Type::default())?;
        left_type = match &left_type.kind {
            TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => {
                if matches!(value_type.kind, TypeKind::Arr(..)) {
                    *value_type.clone()
                } else {
                    left_type.clone()
                }
            }
            _ => left_type.clone(),
        };

        // 处理 Map 类型访问
        if let TypeKind::Map(key_type, value_type) = &left_type.kind {
            // 推导 key 表达式的类型
            self.infer_right_expr(key, *key_type.clone())?;

            // 重写为 MapAccess 节点
            expr.node = AstNode::MapAccess(*key_type.clone(), *value_type.clone(), left.clone(), key.clone());

            return Ok(*value_type.clone());
        }

        // 处理 Vec 和 String 类型访问
        if matches!(left_type.kind, TypeKind::Vec(_) | TypeKind::String) {
            // key 必须是整数类型
            self.infer_right_expr(key, Type::integer_t_new())?;

            let element_type = if let TypeKind::Vec(element_type) = &left_type.kind {
                *element_type.clone()
            } else {
                Type::new(TypeKind::Uint8) // String 的元素类型是 uint8
            };

            // 重写为 VecAccess 节点
            expr.node = AstNode::VecAccess(left_type.clone(), left.clone(), key.clone());

            return Ok(element_type);
        }

        // 处理 Array 类型访问
        if let TypeKind::Arr(_, _, element_type) = &left_type.kind {
            // key 必须是整数类型
            self.infer_right_expr(key, Type::integer_t_new())?;

            // 重写为 ArrayAccess 节点
            expr.node = AstNode::ArrayAccess(*element_type.clone(), left.clone(), key.clone());

            return Ok(*element_type.clone());
        }

        // 处理 Tuple 类型访问
        if let TypeKind::Tuple(elements, _) = &left_type.kind {
            // key 必须是整数字面量
            self.infer_right_expr(key, Type::new(TypeKind::Int))?;

            // 获取索引值
            let index: u64 = if let AstNode::Literal(kind, value) = &key.node {
                if !Type::is_integer(kind) {
                    return Err(AnalyzerError {
                        start: key.start,
                        end: key.end,
                        message: "tuple index must be integer literal".to_string(),
                    });
                }
                value.parse::<u64>().unwrap_or(u64::MAX)
            } else {
                return Err(AnalyzerError {
                    start: key.start,
                    end: key.end,
                    message: "tuple index must be immediate value".to_string(),
                });
            };

            // 检查索引是否越界
            if index >= elements.len() as u64 {
                return Err(AnalyzerError {
                    start: key.start,
                    end: key.end,
                    message: format!("tuple index {} out of range", index),
                });
            }

            let element_type = elements[index as usize].clone();

            // 重写为 TupleAccess 节点
            expr.node = AstNode::TupleAccess(element_type.clone(), left.clone(), index);

            return Ok(element_type);
        }

        // 不支持的类型访问
        Err(AnalyzerError {
            start: expr.start,
            end: expr.end,
            message: format!("access only support map/vec/string/array/tuple, cannot '{}'", left_type),
        })
    }

    /// 尝试推断 enum 成员访问表达式 (如 Color.RED)
    /// 返回 Some((node, type)) 表示成功处理了 enum 成员访问，None 表示不是 enum 访问
    fn try_infer_enum_select(&mut self, left: &Box<Expr>, key: &String, start: usize, end: usize) -> Result<Option<(AstNode, Type)>, AnalyzerError> {
        // 检查 left 是否是 ident
        let AstNode::Ident(left_ident, symbol_id) = &left.node else {
            return Ok(None);
        };

        if *symbol_id == 0 {
            return Ok(None);
        }

        let Some(symbol) = self.symbol_table.get_symbol(*symbol_id) else {
            return Ok(None);
        };

        let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
            return Ok(None);
        };

        // 从锁中提取需要的数据
        let (is_enum, type_expr, typedef_ident) = {
            let typedef = typedef_mutex.lock().unwrap();
            (typedef.is_enum, typedef.type_expr.clone(), typedef.ident.clone())
        };

        if !is_enum {
            return Ok(None);
        }

        // 在释放锁之后调用 reduction_type
        let extract_type = self.reduction_type(type_expr)?;

        let TypeKind::Enum(element_type, properties) = &extract_type.kind else {
            return Ok(None);
        };

        // 查找 enum 成员
        let Some(prop) = properties.iter().find(|p| p.name == *key) else {
            return Err(AnalyzerError {
                start,
                end,
                message: format!("enum '{}' has no member '{}'", left_ident, key),
            });
        };

        let Some(value) = &prop.value else {
            return Err(AnalyzerError {
                start,
                end,
                message: format!("enum '{}' member '{}' has no value", left_ident, key),
            });
        };

        // 返回需要改写的节点和类型
        let new_node = AstNode::Literal(element_type.kind.clone(), value.clone());

        let mut result_type = extract_type.clone();
        result_type.ident = typedef_ident;
        result_type.ident_kind = TypeIdentKind::Def;

        Ok(Some((new_node, result_type)))
    }

    /// 推断 tagged union element 表达式 (用于 is 模式匹配)
    pub fn infer_tagged_union_element(&mut self, expr: &mut Box<Expr>, target_type: Type) -> Result<(), AnalyzerError> {
        let AstNode::TaggedUnionElement(union_type, tagged_name, element) = &mut expr.node else {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: "expected tagged union element".to_string(),
            });
        };

        // 如果 target_type 存在且 union_type 未设置，使用 target_type
        if target_type.kind.is_exist() && !union_type.kind.is_exist() {
            *union_type = target_type.clone();
        }

        // 如果 target_type 有泛型参数但 union_type 没有，使用 target_type 的参数
        if target_type.kind.is_exist() && !target_type.args.is_empty() && union_type.kind.is_exist() && union_type.args.is_empty() {
            if !union_type.ident.is_empty() && union_type.ident != target_type.ident {
                return Err(AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: format!("type inconsistency, expect={}, actual={}", target_type.ident, union_type.ident),
                });
            }
            *union_type = target_type.clone();
        }

        *union_type = self.reduction_type(union_type.clone())?;

        // 获取 tagged union 的元素列表
        let TypeKind::TaggedUnion(_, elements) = &union_type.kind else {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("expected tagged union type, got {}", union_type),
            });
        };

        // 查找匹配的 variant
        let found = elements.iter().find(|e| e.tag == *tagged_name);
        let Some(found_element) = found else {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("enum '{}' has no variant '{}'", union_type.ident, tagged_name),
            });
        };

        // 保存 element 引用
        *element = Some(found_element.clone());
        expr.type_ = union_type.clone();

        Ok(())
    }

    /// 推断 tagged union 构造表达式
    pub fn infer_tagged_union_new(&mut self, expr: &mut Box<Expr>, target_type: Type) -> Result<Type, AnalyzerError> {
        let AstNode::TaggedUnionNew(union_type, tagged_name, element, arg) = &mut expr.node else {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: "expected tagged union new".to_string(),
            });
        };

        // 如果 target_type 存在且 union_type 未设置，使用 target_type
        if target_type.kind.is_exist() && !union_type.kind.is_exist() {
            *union_type = target_type.clone();
        }

        // 如果 target_type 有泛型参数但 union_type 没有，使用 target_type 的参数
        if target_type.kind.is_exist() && !target_type.args.is_empty() && union_type.kind.is_exist() && union_type.args.is_empty() {
            if !union_type.ident.is_empty() && union_type.ident != target_type.ident {
                return Err(AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: format!("type inconsistency, expect={}, actual={}", target_type.ident, union_type.ident),
                });
            }
            *union_type = target_type.clone();
        }

        *union_type = self.reduction_type(union_type.clone())?;

        // 获取 tagged union 的元素列表
        let TypeKind::TaggedUnion(_, elements) = &union_type.kind else {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("expected tagged union type, got {}", union_type),
            });
        };

        // 查找匹配的 variant
        let found = elements.iter().find(|e| e.tag == *tagged_name);
        let Some(found_element) = found else {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("enum '{}' has no variant '{}'", union_type.ident, tagged_name),
            });
        };

        // 保存 element 引用
        *element = Some(found_element.clone());

        // 检查参数是否匹配
        if let Some(arg_expr) = arg {
            self.infer_right_expr(arg_expr, found_element.type_.clone())?;
        }

        // 返回 union 类型
        expr.type_ = union_type.clone();
        Ok(union_type.clone())
    }

    pub fn infer_select_expr(&mut self, expr: &mut Box<Expr>) -> Result<Type, AnalyzerError> {
        let start = expr.start;
        let end = expr.end;
        let AstNode::SelectExpr(left, key, _type_args) = &mut expr.node else {
            unreachable!()
        };

        // 首先检查是否是 enum 类型访问: Color.RED
        if let Some((new_node, enum_type)) = self.try_infer_enum_select(left, key, start, end)? {
            expr.node = new_node;
            return Ok(enum_type);
        }

        // 先推导左侧表达式的类型
        let left_type = self.infer_right_expr(left, Type::default())?;

        // 处理自动解引用 - 如果是指针类型且指向结构体，则获取结构体类型
        let mut deref_type = match &left_type.kind {
            TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => {
                if matches!(value_type.kind, TypeKind::Struct(..)) {
                    *value_type.clone()
                } else {
                    left_type.clone()
                }
            }
            _ => left_type.clone(),
        };

        // 处理结构体类型的属性访问
        if let TypeKind::Struct(_, _, type_properties) = &mut deref_type.kind {
            // 查找属性
            if let Some(property) = type_properties.iter_mut().find(|p| p.name == *key) {
                let property_type = self.reduction_type(property.type_.clone())?;
                property.type_ = property_type.clone();

                // select -> struct select
                // StructSelect(Box<Expr>, String, StructNewProperty), // (instance, key, property)
                expr.node = AstNode::StructSelect(
                    left.clone(),
                    key.clone(),
                    TypeStructProperty {
                        type_: property_type.clone(),
                        name: property.name.clone(),
                        value: property.value.clone(),
                        start: property.start,
                        end: property.end,
                    },
                );

                return Ok(property_type);
            } else {
                // 如果找不到属性，报错
                return Err(AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: format!("type struct '{}' no field '{}'", deref_type.ident, key),
                });
            }
        }

        // 如果不是结构体类型，报错
        Err(AnalyzerError {
            start: expr.start,
            end: expr.end,
            message: format!("no field named '{}' found in type '{}'", key, left_type),
        })
    }

    pub fn infer_async(&mut self, expr: &mut Box<Expr>) -> Result<Type, AnalyzerError> {
        let AstNode::MacroAsync(async_expr) = &mut expr.node else { unreachable!() };
        let start = expr.start;
        let end = expr.end;

        // 处理 flag_expr，如果为 None 则创建一个默认值 0
        if async_expr.flag_expr.is_none() {
            async_expr.flag_expr = Some(Box::new(Expr {
                start,
                end,
                type_: Type::default(),
                target_type: Type::default(),
                node: AstNode::Literal(TypeKind::Int, "0".to_string()),
            }));
        }

        // 推导 flag_expr 类型
        if let Some(flag_expr) = &mut async_expr.flag_expr {
            if let Err(e) = self.infer_right_expr(flag_expr, Type::new(TypeKind::Int)) {
                self.errors_push(e.start, e.end, e.message);
            }
        }

        // 检查原始调用
        self.infer_call(&mut async_expr.origin_call, Type::default(), start, end, false)?;

        let fn_type = async_expr.origin_call.left.type_.clone();

        if let TypeKind::Fn(type_fn) = fn_type.kind {
            async_expr.return_type = type_fn.return_type.clone();
        } else {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: "async expression must call a fn".to_string(),
            });
        }

        // 构造异步调用
        let first_arg = if async_expr.return_type.kind == TypeKind::Void && async_expr.origin_call.args.is_empty() {
            // 如果没有返回值和参数，直接使用原始函数
            let mut left = async_expr.origin_call.left.clone();

            if let TypeKind::Fn(type_fn) = &mut left.type_.kind {
                type_fn.errable = true;
            }

            // 清空闭包函数体以避免推导异常
            {
                async_expr.closure_fn.lock().unwrap().body.stmts.clear();
                async_expr.closure_fn_void.lock().unwrap().body.stmts.clear();
            }

            left
        } else {
            // 需要使用闭包包装
            let _ = self.infer_fn_decl(async_expr.closure_fn.clone(), Type::default());
            let _ = self.infer_fn_decl(async_expr.closure_fn_void.clone(), Type::default());

            let closure_fn = if async_expr.return_type.kind == TypeKind::Void {
                // 使用 void 版本的闭包
                async_expr.closure_fn.lock().unwrap().body.stmts.clear();
                async_expr.closure_fn_void.clone()
            } else {
                async_expr.closure_fn.clone()
            };

            let type_ = closure_fn.lock().unwrap().type_.clone();

            Box::new(Expr {
                start,
                end,
                type_: type_.clone(),
                target_type: type_,
                node: AstNode::FnDef(closure_fn.clone()),
            })
        };

        // typesys 阶段需要保证所有的 ident 都包含 symbol_id, 需要从 global scope 中找到 async 对应的 symbol_id
        let symbol_id = self
            .symbol_table
            .find_symbol_id(&"async".to_string(), self.symbol_table.global_scope_id)
            .unwrap();

        // 构造 async 调用
        let call = AstCall {
            return_type: Type::default(),
            left: Box::new(Expr::ident(start, end, "async".to_string(), symbol_id)),
            args: vec![first_arg, async_expr.flag_expr.clone().unwrap()],
            generics_args: vec![async_expr.return_type.clone()],
            spread: false,
        };

        expr.node = AstNode::Call(call);
        let result = self.infer_right_expr(expr, Type::default())?;
        return Ok(result);
    }

    pub fn infer_expr(&mut self, expr: &mut Box<Expr>, infer_target_type: Type, literal_refer: Type) -> Result<Type, AnalyzerError> {
        if expr.type_.kind.is_exist() {
            return Ok(expr.type_.clone());
        }

        if matches!(expr.node, AstNode::SelectExpr(..)) {
            if self.try_rewrite_tagged_union_select(expr, &infer_target_type)? {
                return self.infer_tagged_union_new(expr, infer_target_type);
            }
        }

        if matches!(expr.node, AstNode::Call(_)) {
            if self.try_rewrite_tagged_union_call(expr, &infer_target_type)? {
                return self.infer_tagged_union_new(expr, infer_target_type);
            }
        }

        return match &mut expr.node {
            AstNode::As(_, _, _) => self.infer_as_expr(expr),
            AstNode::Catch(try_expr, catch_err_mutex, catch_body) => self.infer_catch(try_expr, catch_err_mutex, catch_body),
            AstNode::Match(subject, cases) => self.infer_match(subject, cases, infer_target_type, expr.start, expr.end),
            AstNode::Is(target_type, union_tag, src, _binding) => {
                if let Some(src_expr) = src {
                    let src_type = self.infer_right_expr(src_expr, Type::default())?;

                    if !matches!(src_type.kind, TypeKind::Union(..) | TypeKind::TaggedUnion(..) | TypeKind::Interface(..)) {
                        return Err(AnalyzerError {
                            start: expr.start,
                            end: expr.end,
                            message: format!("{} cannot use 'is' operator", src_type),
                        });
                    }

                    // 处理 tagged union 的 union_tag
                    if let Some(ut) = union_tag {
                        if !matches!(src_type.kind, TypeKind::TaggedUnion(..)) {
                            return Err(AnalyzerError {
                                start: expr.start,
                                end: expr.end,
                                message: "unexpected is expr".to_string(),
                            });
                        }
                        self.infer_tagged_union_element(ut, src_type)?;
                    }
                }

                // 处理 target_type (如果存在)
                if target_type.kind.is_exist() {
                    *target_type = self.reduction_type(target_type.clone())?;
                }

                return Ok(Type::new(TypeKind::Bool));
            }
            AstNode::TaggedUnionNew(..) => {
                return self.infer_tagged_union_new(expr, infer_target_type);
            }
            AstNode::MacroSizeof(target_type) => {
                *target_type = self.reduction_type(target_type.clone())?;

                // literal 自动转换
                if Type::is_integer(&infer_target_type.kind) || infer_target_type.kind == TypeKind::Anyptr {
                    return Ok(infer_target_type.clone());
                }

                return Ok(Type::new(TypeKind::Int));
            }
            AstNode::MacroDefault(target_type) => {
                *target_type = self.reduction_type(target_type.clone())?;
                return Ok(target_type.clone());
            }
            AstNode::MacroReflectHash(target_type) => {
                *target_type = self.reduction_type(target_type.clone())?;
                Ok(Type::new(TypeKind::Int))
            }
            AstNode::New(type_, properties, expr_option) => {
                *type_ = self.reduction_type(type_.clone())?;

                if let TypeKind::Struct(_, _, type_properties) = &mut type_.kind {
                    *properties = self.infer_struct_properties(type_properties, properties, expr.start, expr.end)?;
                } else {
                    // check scala type or arr
                    if !Type::is_scala_type(&type_.kind) && !matches!(type_.kind, TypeKind::Arr(..)) {
                        return Err(AnalyzerError {
                            start: expr.start,
                            end: expr.end,
                            message: "'new' operator can only be used with scalar types (number/boolean/struct/array)".to_string(),
                        });
                    }

                    if let Some(expr) = expr_option {
                        self.infer_right_expr(expr, type_.clone())?;
                    }
                }

                return self.reduction_type(Type::ref_of(type_.clone()));
            }
            AstNode::Binary(op, left, right) => self.infer_binary(op.clone(), left, right, infer_target_type),
            AstNode::Ternary(condition, consequent, alternate) => self.infer_ternary(condition, consequent, alternate, infer_target_type),
            AstNode::Unary(op, operand) => self.infer_unary(op.clone(), operand, infer_target_type),
            AstNode::Ident(ident, symbol_id) => self.infer_ident(ident, symbol_id, expr.start, expr.end),
            AstNode::VecNew(..) => self.infer_vec_new(expr, infer_target_type, literal_refer),
            AstNode::VecRepeatNew(..) | AstNode::ArrRepeatNew(..) => self.infer_vec_repeat_new(expr, infer_target_type),
            AstNode::VecSlice(left, start, end) => {
                let left_type = self.infer_right_expr(left, Type::default())?;
                self.infer_right_expr(start, Type::integer_t_new())?;
                self.infer_right_expr(end, Type::integer_t_new())?;

                return Ok(left_type.clone());
            }
            AstNode::EmptyCurlyNew => {
                if Type::is_any(&literal_refer.kind) {
                    return Ok(Type::new(TypeKind::Map(
                        Box::new(Type::new(TypeKind::String)),
                        Box::new(infer_target_type.clone()),
                    )));
                }

                // 必须通过 target_type 引导才能推断出具体的类型, 所以 target_type kind 必须存在
                match &infer_target_type.kind {
                    TypeKind::Map(_, _) => {
                        expr.node = AstNode::MapNew(Vec::new());
                    }
                    TypeKind::Set(_) => {
                        // change expr kind to set new
                        expr.node = AstNode::SetNew(Vec::new());
                    }
                    _ => {
                        return Err(AnalyzerError {
                            start: expr.start,
                            end: expr.end,
                            message: format!("empty curly new cannot ref type {}", infer_target_type),
                        });
                    }
                }

                return Ok(infer_target_type);
            }
            AstNode::MapNew(elements) => self.infer_map_new(elements, infer_target_type, literal_refer, expr.start, expr.end),
            AstNode::SetNew(elements) => self.infer_set_new(elements, infer_target_type, expr.start, expr.end),
            AstNode::TupleNew(elements) => self.infer_tuple_new(elements, infer_target_type, expr.start, expr.end),
            AstNode::StructNew(_ident, type_, properties) => {
                *type_ = self.reduction_type(type_.clone())?;

                if let TypeKind::Struct(_, _, type_properties) = &mut type_.kind {
                    *properties = self.infer_struct_properties(type_properties, properties, expr.start, expr.end)?;
                } else {
                    return Err(AnalyzerError {
                        start: expr.start,
                        end: expr.end,
                        message: format!("cannot use 'new' operator on non-struct type {}", type_),
                    });
                }

                return Ok(type_.clone());
            }
            AstNode::AccessExpr(..) => self.infer_access_expr(expr),
            AstNode::SelectExpr(..) => self.infer_select_expr(expr),
            AstNode::Call(call) => self.infer_call(call, infer_target_type, expr.start, expr.end, true),
            AstNode::MacroAsync(_) => self.infer_async(expr),
            AstNode::FnDef(fndef) => self.infer_fn_decl(fndef.clone(), infer_target_type),
            AstNode::Literal(..) => self.infer_literal(expr, infer_target_type),
            AstNode::EnvAccess(_, unique_ident, symbol_id_option) => {
                return self.infer_ident(unique_ident, symbol_id_option, expr.start, expr.end);
            }
            _ => {
                return Err(AnalyzerError {
                    start: 0,
                    end: 0,
                    message: "unknown operand".to_string(),
                });
            }
        };
    }

    fn is_nullable_interface(&mut self, target_type: &Type) -> bool {
        if let TypeKind::Union(_, nullable, elements) = &target_type.kind {
            if !nullable {
                return false;
            }

            debug_assert!(elements.len() == 2);
            let first_element = &elements[0];
            return matches!(first_element.kind, TypeKind::Interface(..));
        } else {
            return false;
        }
    }

    fn as_to_interface(&mut self, expr: &mut Box<Expr>, interface_type: Type) -> Result<Box<Expr>, AnalyzerError> {
        debug_assert!(matches!(interface_type.ident_kind, TypeIdentKind::Interface));
        debug_assert!(expr.type_.status == ReductionStatus::Done);
        debug_assert!(interface_type.status == ReductionStatus::Done);

        // 自动解引用指针类型
        let src_type = match &expr.type_.kind {
            TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => *value_type.clone(),
            _ => expr.type_.clone(),
        };

        // 检查源类型必须是已定义类型
        if !matches!(src_type.ident_kind, TypeIdentKind::Def) {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("type '{}' cannot casting to interface '{}'", src_type, interface_type.ident),
            });
        }

        if src_type.symbol_id == 0 {
            return Err(AnalyzerError {
                start: 0,
                end: 0,
                message: "src type symbol id is zero".to_string(),
            });
        }

        // 获取类型定义
        let symbol = self.symbol_table.get_symbol(src_type.symbol_id).unwrap();
        let SymbolKind::Type(typedef_stmt_mutex) = symbol.kind.clone() else {
            unreachable!()
        };
        let typedef_stmt = typedef_stmt_mutex.lock().unwrap();

        // 检查类型是否实现了目标接口
        let found = self.check_impl_interface_contains(&typedef_stmt, &interface_type);

        // 禁止鸭子类型
        if !found {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("type '{}' not impl '{}' interface", src_type.ident, interface_type.ident),
            });
        }

        // 检查接口实现的完整性
        self.check_typedef_impl(&interface_type, src_type.ident.clone(), &typedef_stmt)
            .map_err(|e| AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: e,
            })?;

        // 创建类型转换表达式
        Ok(Box::new(Expr {
            start: expr.start,
            end: expr.end,
            type_: interface_type.clone(),
            target_type: interface_type.clone(),
            node: AstNode::As(interface_type.clone(), None, expr.clone()),
        }))
    }

    fn infer_literal(&mut self, expr: &mut Box<Expr>, infer_target_type: Type) -> Result<Type, AnalyzerError> {
        let AstNode::Literal(kind, literal_value) = &mut expr.node else {
            unreachable!()
        };

        let literal_type = self.reduction_type(Type::new(kind.clone()))?;
        *kind = literal_type.kind.clone();

        let mut target_kind = infer_target_type.kind.clone();
        if matches!(target_kind, TypeKind::Anyptr) {
            target_kind = TypeKind::Uint;
        }
        target_kind = Type::cross_kind_trans(&target_kind);

        if Type::is_float(&literal_type.kind) && Type::is_float(&target_kind) {
            *kind = target_kind; // auto casting
            return Ok(infer_target_type);
        }

        // int literal 自动转换为 float 类型
        // float 不能转换为 int, 会导致数据丢失
        if Type::is_integer(&literal_type.kind) && Type::is_float(&target_kind) {
            *kind = target_kind;
            return Ok(infer_target_type);
        }

        if Type::is_integer(&literal_type.kind) && Type::is_integer(&target_kind) {
            let (literal_value, is_negative) = if literal_value.starts_with('-') {
                (literal_value[1..].to_string(), true)
            } else {
                (literal_value.clone(), false)
            };

            let mut i = if literal_value.starts_with("0x") {
                i64::from_str_radix(&literal_value[2..], 16)
            } else if literal_value.starts_with("0b") {
                i64::from_str_radix(&literal_value[2..], 2)
            } else if literal_value.starts_with("0o") {
                i64::from_str_radix(&literal_value[2..], 8)
            } else {
                literal_value.parse::<i64>()
            }
            .map_err(|e| AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: e.to_string(),
            })?;

            if is_negative {
                i = -i;
            }

            if self.integer_range_check(&target_kind, i) {
                *kind = target_kind;
                return Ok(infer_target_type);
            }

            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("literal {} out of range for type '{}'", literal_value, infer_target_type),
            });
        }

        return Ok(literal_type);
    }

    fn integer_range_check(&self, kind: &TypeKind, value: i64) -> bool {
        match kind {
            TypeKind::Int8 => value >= i8::MIN as i64 && value <= i8::MAX as i64,
            TypeKind::Int16 => value >= i16::MIN as i64 && value <= i16::MAX as i64,
            TypeKind::Int32 => value >= i32::MIN as i64 && value <= i32::MAX as i64,
            TypeKind::Int64 => value >= i64::MIN as i64 && value <= i64::MAX as i64,
            TypeKind::Uint8 => value >= 0 && value <= u8::MAX as i64,
            TypeKind::Uint16 => value >= 0 && value <= u16::MAX as i64,
            TypeKind::Uint32 => value >= 0 && value <= u32::MAX as i64,
            TypeKind::Uint64 => value >= 0,
            _ => false,
        }
    }

    fn can_assign_to_interface(&self, type_: &Type) -> bool {
        match &type_.kind {
            TypeKind::Interface(..) => false,
            TypeKind::Void => false,
            TypeKind::Null => false,
            _ => true,
        }
    }

    fn can_assign_to_union(&self, type_: &Type) -> bool {
        match &type_.kind {
            TypeKind::Union(..) => false,
            TypeKind::Void => false,
            _ => true,
        }
    }

    fn union_type_contains(&mut self, union: &(bool, Vec<Type>), sub: &Type) -> bool {
        if union.0 {
            return true;
        }

        union.1.iter().any(|item| self.type_compare(item, sub))
    }

    pub fn infer_right_expr(&mut self, expr: &mut Box<Expr>, mut target_type: Type) -> Result<Type, AnalyzerError> {
        if expr.type_.kind.is_unknown() {
            let t = self.infer_expr(
                expr,
                if !matches!(target_type.kind, TypeKind::Union(..)) {
                    target_type.clone()
                } else {
                    Type::default()
                },
                if Type::is_any(&target_type.kind) {
                    target_type.clone()
                } else {
                    Type::default()
                },
            )?;
            target_type = self.reduction_type(target_type.clone())?;
            expr.type_ = self.reduction_type(t)?;
            expr.target_type = target_type.clone();
        }

        if target_type.kind.is_unknown() {
            return Ok(expr.type_.clone());
        }

        // handle interface
        if matches!(target_type.kind, TypeKind::Interface(..)) && self.can_assign_to_interface(&expr.type_) {
            *expr = self.as_to_interface(expr, target_type.clone())?;
        }

        // if self.null
        if self.is_nullable_interface(&target_type) && self.can_assign_to_interface(&expr.type_) {
            let TypeKind::Union(_, nullable, elements) = target_type.kind.clone() else {
                unreachable!()
            };

            debug_assert!(nullable);
            debug_assert!(elements.len() == 2);
            let interface_type = elements[0].clone();

            *expr = self.as_to_interface(expr, interface_type)?;
        }

        // auto as，为了后续的 linear, 需要将 signal type 转换为 union type
        if matches!(target_type.kind, TypeKind::Union(..)) && self.can_assign_to_union(&expr.type_) {
            let TypeKind::Union(any, _, elements) = &target_type.kind else {
                unreachable!()
            };
            if !self.union_type_contains(&(*any, elements.clone()), &expr.type_) {
                return Err(AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: format!("union type not contains '{}'", expr.type_),
                });
            }

            // expr 改成成 union 类型
            expr.node = AstNode::As(target_type.clone(), None, expr.clone());
            expr.type_ = target_type.clone();
            expr.target_type = target_type.clone();
        }

        // 最后进行类型比较
        if !self.type_compare(&target_type, &expr.type_) {
            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("type inconsistency: expect '{}', actual '{}'", target_type, expr.type_),
            });
        }

        Ok(expr.type_.clone())
    }

    pub fn infer_left_expr(&mut self, expr: &mut Box<Expr>) -> Result<Type, AnalyzerError> {
        let type_result = match &mut expr.node {
            // 标识符
            AstNode::Ident(ident, symbol_id) => self.infer_ident(ident, symbol_id, expr.start, expr.end),

            // 元组解构
            AstNode::TupleDestr(elements) => {
                let mut element_types = Vec::new();
                for element in elements.iter_mut() {
                    let element_type = self.infer_left_expr(element)?;
                    debug_assert!(element_type.kind.is_exist());

                    element_types.push(element_type);
                }
                self.reduction_type(Type::undo_new(TypeKind::Tuple(element_types, 0)))
            }

            // 访问表达式
            AstNode::AccessExpr(..) => self.infer_access_expr(expr),

            // 选择表达式
            AstNode::SelectExpr(..) => self.infer_select_expr(expr),

            // 环境变量访问
            AstNode::EnvAccess(_, ident, symbol_id_option) => self.infer_ident(ident, symbol_id_option, expr.start, expr.end),

            // 函数调用
            AstNode::Call(call) => self.infer_call(call, Type::default(), expr.start, expr.end, true),

            // *a
            AstNode::Unary(op, operand) => {
                if *op == ExprOp::Ia {
                    return self.infer_unary(op.clone(), operand, Type::default());
                } else {
                    return Err(AnalyzerError {
                        start: expr.start,
                        end: expr.end,
                        message: "unary operand cannot used in left".to_string(),
                    });
                }
            }

            _ => Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: "operand cannot be used as left value".to_string(),
            }),
        };

        return match type_result {
            Ok(t) => {
                if !t.kind.is_exist() {
                    debug_assert!(false, "infer left type not exist");
                }
                expr.type_ = t.clone();
                Ok(t.clone())
            }
            Err(e) => {
                return Err(e);
            }
        };
    }

    pub fn infer_vardef(&mut self, var_decl_mutex: &Arc<Mutex<VarDeclExpr>>, right_expr: &mut Box<Expr>) -> Result<(), AnalyzerError> {
        {
            let mut var_decl = var_decl_mutex.lock().unwrap();
            var_decl.type_ = self.reduction_type(var_decl.type_.clone())?;
        }

        // 重写变量声明(处理泛型情况下的变量名重写)
        self.rewrite_var_decl(var_decl_mutex.clone());

        // 检查变量类型是否为void(非参数类型的情况下)
        {
            let var_decl = var_decl_mutex.lock().unwrap();
            if var_decl.type_.ident_kind != TypeIdentKind::GenericsParam && matches!(var_decl.type_.kind, TypeKind::Void) {
                return Err(AnalyzerError {
                    start: var_decl.symbol_start,
                    end: var_decl.symbol_end,
                    message: "cannot assign to void".to_string(),
                });
            }
        }

        // 获取变量声明的类型用于右值表达式的类型推导
        let mut var_decl = var_decl_mutex.lock().unwrap();

        // 推导右值表达式的类型
        let right_type = self.infer_right_expr(right_expr, var_decl.type_.clone())?;

        // 检查右值类型是否为void
        if matches!(right_type.kind, TypeKind::Void) {
            return Err(AnalyzerError {
                start: right_expr.start,
                end: right_expr.end,
                message: "cannot assign void to var".to_string(),
            });
        }

        if matches!(var_decl.type_.kind, TypeKind::Unknown) {
            // 检查右值类型是否已确定
            if !self.type_confirm(&right_type) {
                return Err(AnalyzerError {
                    start: right_expr.start,
                    end: right_expr.end,
                    message: "stmt right type not confirmed".to_string(),
                });
            }

            // 使用右值类型作为变量类型
            var_decl.type_ = right_type;
        }

        Ok(())
    }

    pub fn infer_var_tuple_destr(&mut self, elements: &mut Vec<Box<Expr>>, right_type: Type, start: usize, end: usize) -> Result<(), AnalyzerError> {
        // check length
        // right_type.kind
        let TypeKind::Tuple(type_elements, _type_align) = right_type.kind else {
            unreachable!()
        };

        if type_elements.len() != elements.len() {
            return Err(AnalyzerError {
                start,
                end,
                message: format!("tuple length mismatch, expect {}, got {}", type_elements.len(), elements.len()),
            });
        }

        // 遍历按顺序对比类型,并且顺便 rewrite
        for (i, expr) in elements.iter_mut().enumerate() {
            let target_type = type_elements[i].clone();
            if !self.type_confirm(&target_type) {
                self.errors_push(expr.start, expr.end, format!("tuple operand index '{}' type not confirm", i));
                continue;
            }

            debug_assert!(target_type.kind.is_exist());
            expr.type_ = target_type.clone();

            // 递归处理
            if let AstNode::VarDecl(var_decl_mutex) = &expr.node {
                {
                    let mut var_decl = var_decl_mutex.lock().unwrap();
                    if var_decl.symbol_id == 0 {
                        return Err(AnalyzerError {
                            start: 0,
                            end: 0,
                            message: "var symbol id is zero, cannot infer".to_string(),
                        });
                    }

                    var_decl.type_ = target_type.clone();
                    debug_assert!(var_decl.type_.kind.is_exist());
                }

                self.rewrite_var_decl(var_decl_mutex.clone());
            } else if let AstNode::TupleDestr(sub_elements) = &mut expr.node {
                if let Err(e) = self.infer_var_tuple_destr(sub_elements, target_type, expr.start, expr.end) {
                    self.errors_push(e.start, e.end, e.message);
                }
            } else {
                self.errors_push(expr.start, expr.end, format!("var tuple destr mut var or tuple_destr"));
            }
        }

        Ok(())
    }

    pub fn infer_try_catch(
        &mut self,
        try_body: &mut AstBody,
        catch_err_mutex: &Arc<Mutex<VarDeclExpr>>,
        catch_body: &mut AstBody,
    ) -> Result<(), AnalyzerError> {
        self.be_caught += 1;

        self.infer_body(try_body);

        self.be_caught -= 1;

        {
            let mut catch_err = catch_err_mutex.lock().unwrap();
            let errort = self.interface_throwable();
            catch_err.type_ = errort;
        }

        self.rewrite_var_decl(catch_err_mutex.clone());

        // set break target types
        self.ret_target_types.push(Type::new(TypeKind::Void));
        self.infer_body(catch_body);
        self.ret_target_types.pop().unwrap();

        Ok(())
    }

    pub fn infer_catch(
        &mut self,
        try_expr: &mut Box<Expr>,
        catch_err_mutex: &Arc<Mutex<VarDeclExpr>>,
        catch_body: &mut AstBody,
    ) -> Result<Type, AnalyzerError> {
        self.be_caught += 1;

        let right_type = self.infer_right_expr(try_expr, Type::default())?;

        self.be_caught -= 1;

        // reduction errort
        {
            let mut catch_err = catch_err_mutex.lock().unwrap();
            let errort = self.interface_throwable();
            catch_err.type_ = errort;
        }

        self.rewrite_var_decl(catch_err_mutex.clone());

        // set break target types
        self.ret_target_types.push(right_type);
        self.infer_body(catch_body);
        return Ok(self.ret_target_types.pop().unwrap());
    }

    pub fn generics_impl_args_hash(&mut self, args: &Vec<Type>) -> Result<u64, AnalyzerError> {
        let mut hash_str = "fn.".to_string();

        // 遍历所有泛型参数类型
        for arg in args {
            // 对每个参数类型进行还原
            let reduced_type = self.reduction_type(arg.clone())?;
            // 将类型的哈希值添加到字符串中
            hash_str.push_str(&reduced_type.hash().to_string());
        }

        // 计算最终的哈希值
        let mut hasher = DefaultHasher::new();
        hash_str.hash(&mut hasher);
        Ok(hasher.finish())
    }

    fn find_impl_call_ident(&mut self, impl_symbol_name: String, impl_args: Vec<Type>, select_left_type: &Type) -> Result<(String, NodeId), String> {
        // 定位 scope_id
        let global_symbol_option = self.symbol_table.find_global_symbol(&impl_symbol_name);
        let module_scope_id = if let Some(global_symbol) = global_symbol_option {
            global_symbol.defined_in
        } else {
            // 直接使用 global scope id 进行测试？应该问题不大, 后面还会报错
            self.symbol_table.global_scope_id
        };

        // 如果有泛型参数,需要处理hash
        if impl_args.len() > 0 {
            // 基于 arg 计算 unique hash
            let arg_hash = self.generics_impl_args_hash(&impl_args).map_err(|e| e.message)?;

            let impl_symbol_name_with_hash = format_generics_ident(impl_symbol_name.clone(), arg_hash);

            // find by hash
            if let Some(symbol_id) = self.symbol_table.find_symbol_id(&impl_symbol_name_with_hash, module_scope_id) {
                return Ok((impl_symbol_name_with_hash, symbol_id));
            }
        }

        // 直接通过 impl_symbol_name 不携带 hash
        if let Some(symbol_id) = self.symbol_table.find_symbol_id(&impl_symbol_name, module_scope_id) {
            return Ok((impl_symbol_name, symbol_id));
        }

        return Err(format!("type '{}' not impl fn'{}'", select_left_type, impl_symbol_name));
    }

    fn generics_args_table(
        &mut self,
        call_data: (Vec<Box<Expr>>, Vec<Type>, bool),
        return_target_type: Type,
        temp_fndef_mutex: Arc<Mutex<AstFnDef>>,
    ) -> Result<HashMap<String, Type>, String> {
        let (mut args, generics_args, spread) = call_data;
        let mut table = HashMap::new();

        let mut temp_fndef = temp_fndef_mutex.lock().unwrap();

        debug_assert!(temp_fndef.is_generics);

        let Some(generics_params) = temp_fndef.generics_params.as_ref() else {
            return Err("generics params missing".to_string());
        };
        let explicit_args_len = generics_args.len();
        if explicit_args_len > generics_params.len() {
            return Err("too many generics args".to_string());
        }

        // 先按顺序灌入显式泛型参数，剩余参数再自动推导（支持 impl 类型参数前缀 + 方法参数推导）
        for i in 0..explicit_args_len {
            let reduced_type = self.reduction_type(generics_args[i].clone()).map_err(|e| e.message)?;
            table.insert(generics_params[i].ident.clone(), reduced_type);
        }

        if explicit_args_len < generics_params.len() {
            // 保存当前的类型参数栈
            let stash_stack = self.generics_args_stack.clone();
            self.generics_args_stack.clear();

            let mut formal_param_offset = 0;
            // impl 方法调用在 generics 特化阶段尚未注入 self 参数
            if temp_fndef.is_impl && !temp_fndef.is_static && temp_fndef.self_kind != SelfKind::Null {
                formal_param_offset = 1;
            }

            // 遍历所有参数进行类型推导
            let args_len = args.len();
            for (i, arg) in args.iter_mut().enumerate() {
                let is_spread = spread && (i == args_len - 1);

                // 获取实参类型 (arg 在 infer right expr 中会被修改 type_ 和 target_type 字段)
                let arg_type = self.infer_right_expr(arg, Type::default()).map_err(|e| e.message)?;

                let formal_type = self.select_generics_fn_param(temp_fndef.clone(), i + formal_param_offset, is_spread);
                if formal_type.err {
                    return Err("too many arguments".to_string());
                }
                // 对形参类型进行还原
                let temp_type = self.reduction_type(formal_type.clone()).map_err(|e| e.message)?;

                // 比较类型并填充泛型参数表
                if !self.type_generics(&temp_type, &arg_type, &mut table) {
                    return Err(format!("cannot generics type from {} to {}", arg_type, temp_type));
                }
            }

            // 处理返回类型约束
            if !matches!(
                return_target_type.kind,
                TypeKind::Unknown | TypeKind::Void | TypeKind::Union(..) | TypeKind::Null
            ) {
                let temp_type = self.reduction_type(temp_fndef.return_type.clone()).map_err(|e| e.message)?;

                if !self.type_generics(&temp_type, &return_target_type, &mut table) {
                    return Err(format!("cannot generics type from {} to {}", &temp_fndef.return_type, temp_type));
                }
            }

            // 恢复类型参数栈
            self.generics_args_stack = stash_stack;
        }

        // 判断泛型参数是否全部推断完成，并做约束校验
        if let Some(generics_params) = &mut temp_fndef.generics_params {
            for param in generics_params {
                let arg_type = match table.get(&param.ident) {
                    Some(arg_type) => arg_type,
                    None => return Err(format!("cannot infer generics param '{}'", param.ident)),
                };

                self.generics_constrains_check(param, arg_type)?
            }
        }

        Ok(table)
    }

    fn generics_args_hash(&mut self, generics_params: &Vec<GenericsParam>, args_table: HashMap<String, Type>) -> u64 {
        let mut hash_str = "fn.".to_string();

        // 遍历所有泛型参数
        for param in generics_params {
            // 从args_table中获取对应的类型
            let t = args_table
                .get(&param.ident)
                .unwrap_or_else(|| panic!("cannot infer generics param '{}'", param.ident));

            hash_str.push_str(&t.hash().to_string());
        }

        let mut hasher = DefaultHasher::new();
        hash_str.hash(&mut hasher);
        hasher.finish()
    }

    fn generics_special_fn(
        &mut self,
        call_data: (Vec<Box<Expr>>, Vec<Type>, bool),
        target_type: Type,
        temp_fndef_mutex: Arc<Mutex<AstFnDef>>,
        module_scope_id: NodeId,
    ) -> Result<Arc<Mutex<AstFnDef>>, String> {
        {
            let temp_fndef = temp_fndef_mutex.lock().unwrap();
            debug_assert!(!temp_fndef.is_local);
            debug_assert!(temp_fndef.is_generics);
            debug_assert!(temp_fndef.generics_params.is_some());
        }

        let args_table: HashMap<String, Type> = self.generics_args_table(call_data, target_type, temp_fndef_mutex.clone())?;

        let args_hash = {
            let temp_fndef = temp_fndef_mutex.lock().unwrap();
            self.generics_args_hash(temp_fndef.generics_params.as_ref().unwrap(), args_table.clone())
        };

        let symbol_name_with_hash = {
            let temp_fndef = temp_fndef_mutex.lock().unwrap();
            //temp_fndef.symbol_name#args_hash
            format_generics_ident(temp_fndef.symbol_name.clone(), args_hash)
        };

        // 对齐编译器：已存在相同 args_hash 的特化函数时直接复用，避免重复 clone/注册。
        if let Some(symbol) = self.symbol_table.find_symbol(&symbol_name_with_hash, module_scope_id) {
            let SymbolKind::Fn(fn_mutex) = &symbol.kind else {
                return Err(format!("symbol {} kind not fn", symbol_name_with_hash));
            };

            let is_specialized = {
                let fndef = fn_mutex.lock().unwrap();
                fndef.generics_args_hash == Some(args_hash)
            };
            if is_specialized {
                return Ok(fn_mutex.clone());
            }
        }

        // 泛型重载支持核心逻辑
        // 在没有基于类型约束产生重载的情况下，temp_fn 就是 tpl_fn
        // 在存在重载的情况下, 应该基于 arg_hash 查找具体类型的 tpl_fn
        // 即使是 tpl 有强制类型的约束，但是如果存在多个约束的情况下也会需要多个 tpl fn, 所以最好无论什么情况，都进行 tpl clone 重新生成，以及符号表的覆盖操作
        // 后续类型约束按照 tpl 进行了符号覆盖, 符号覆盖将会导致 tpl 查找异常。
        // 基于 temp_fndef deep clone 生成 special_fn 后，下一次如果存在相同类型的 arg_hash, 则会直接读取到 special_fn。
        let tpl_fn_mutex = if let Some(symbol) = self.symbol_table.find_symbol(&symbol_name_with_hash, module_scope_id) {
            let SymbolKind::Fn(fn_mutex) = &symbol.kind else {
                return Err(format!("symbol {} kind not fn", symbol_name_with_hash));
            };
            fn_mutex.clone()
        } else {
            temp_fndef_mutex
        };

        // lsp 中无论是 否 singleton 都会 clone 一份, 因为 ide 会随时会修改文件从而新增泛型示例，必须保证 tpl fn 是无污染的
        let special_fn_mutex = GenericSpecialFnClone { global_parent: None }.deep_clone(&tpl_fn_mutex);

        {
            let mut special_fn = special_fn_mutex.lock().unwrap();

            special_fn.generics_args_hash = Some(args_hash);
            special_fn.generics_args_table = Some(args_table);
            special_fn.symbol_name = symbol_name_with_hash; // special_fn 此时已经拥有崭新的名称，接下来只需要注册到所在 module 符号表即可
            debug_assert!(!special_fn.is_local);

            // singleton_tpl 则是同一个符号，不需要重复注册，直接使用即可
            // 非 singleton_tpl 对于当前 symbol_name 必须是唯一的，不能重复注册。存在缓存机制，也不可能重复注册
            let new_symbol_id = self.symbol_table.cover_symbol_in_scope(
                special_fn.symbol_name.clone(),
                SymbolKind::Fn(special_fn_mutex.clone()),
                special_fn.symbol_start,
                module_scope_id,
            );

            special_fn.symbol_id = new_symbol_id;
            special_fn.type_.status = ReductionStatus::Undo;
            special_fn.return_type.status = ReductionStatus::Undo;
            // set type_args_stack
            self.generics_args_stack.push(special_fn.generics_args_table.clone().unwrap());
        }

        self.infer_fn_decl(special_fn_mutex.clone(), Type::default()).map_err(|e| e.message)?;

        self.worklist.push(special_fn_mutex.clone());

        // handle child
        {
            let special_fn = special_fn_mutex.lock().unwrap();
            for child in special_fn.local_children.iter() {
                // local fn 定义在 global fn 中，所以其 define_in != global_fn 的 scope_id
                self.rewrite_local_fndef(child.clone());
                self.infer_fn_decl(child.clone(), Type::default()).map_err(|e| e.message)?;
            }
        }

        self.generics_args_stack.pop();
        Ok(special_fn_mutex)
    }

    fn rewrite_local_fndef(&mut self, fndef_mutex: Arc<Mutex<AstFnDef>>) {
        let mut local_fndef = fndef_mutex.lock().unwrap();

        debug_assert!(local_fndef.symbol_id > 0);

        // 已经注册并改写完毕，不需要重复改写
        if local_fndef.generics_args_hash.is_some() {
            return;
        }

        // local fn 直接使用 parent 的 hash
        // 这么做也是为了兼容 generic 的情况
        // 否则 local fn 根本不会存在同名的情况, 另外 local fn 的调用作用域仅仅在当前函数内
        if let Some(global_parent) = &local_fndef.global_parent {
            let args_hash = global_parent.lock().unwrap().generics_args_hash.unwrap();
            local_fndef.generics_args_hash = Some(args_hash);
        } else {
            debug_assert!(false);
        }

        // 重写函数名并在符号表中
        local_fndef.symbol_name = format_generics_ident(local_fndef.symbol_name.clone(), local_fndef.generics_args_hash.unwrap());

        let scope_id = self.symbol_table.find_scope_id(local_fndef.symbol_id);

        match self.symbol_table.define_symbol_in_scope(
            local_fndef.symbol_name.clone(),
            SymbolKind::Fn(fndef_mutex.clone()),
            local_fndef.symbol_start,
            scope_id,
        ) {
            Ok(symbol_id) => {
                local_fndef.symbol_id = symbol_id;
            }
            Err(_e) => {}
        }
    }

    /**
     * 整个 infer 都是在 global scope 中进行的，泛型函数也同样只能在全局函数中生命
     */
    #[allow(dead_code)]
    fn infer_generics_special(
        &mut self,
        target_type: Type,
        symbol_id: NodeId,
        call_data: (Vec<Box<Expr>>, Vec<Type>, bool),
    ) -> Result<Option<Arc<Mutex<AstFnDef>>>, String> {
        // cal target generics fn symbol_id
        let symbol = self.symbol_table.get_symbol(symbol_id).unwrap();
        let scope_id = symbol.defined_in;

        if let SymbolKind::Fn(fndef_mutex) = symbol.kind.clone() {
            {
                let fndef = fndef_mutex.lock().unwrap();
                if fndef.is_local {
                    return Ok(None);
                }

                if !fndef.is_generics {
                    return Ok(None);
                }
            }

            // 由于存在函数重载，所以需要进行多次匹配找到最合适的 is_tpl 函数。
            let special_fn = self.generics_special_fn(call_data, target_type, fndef_mutex, scope_id)?;
            return Ok(Some(special_fn));
        }

        return Ok(None);
    }

    pub fn select_generics_fn_param(&mut self, temp_fndef: AstFnDef, index: usize, is_spread: bool) -> Type {
        // let temp_fndef = temp_fndef.lock().unwrap();
        debug_assert!(temp_fndef.is_generics);

        if temp_fndef.rest_param && index >= temp_fndef.params.len() - 1 {
            let last_param_mutex = temp_fndef.params.last().unwrap();
            let last_param_type = last_param_mutex.lock().unwrap().type_.clone();

            if let TypeKind::Vec(element_type) = &last_param_type.kind {
                if is_spread {
                    return last_param_type.clone();
                }

                return *element_type.clone();
            } else {
                debug_assert!(false, "last param type must be vec");
            }
        }

        if index >= temp_fndef.params.len() {
            return Type::error();
        }

        let param = temp_fndef.params[index].lock().unwrap();
        return param.type_.clone();
    }

    pub fn select_fn_param(&mut self, index: usize, target_type_fn: TypeFn, is_spread: bool) -> Type {
        if target_type_fn.rest && index >= target_type_fn.param_types.len() - 1 {
            let last_param_type = target_type_fn.param_types.last().unwrap();

            // 最后一个参数必须是 vec
            debug_assert!(matches!(last_param_type.kind, TypeKind::Vec(..)));

            if is_spread {
                return last_param_type.clone();
            }

            if let TypeKind::Vec(element_type) = &last_param_type.kind {
                return *element_type.clone();
            }
        }

        if index >= target_type_fn.param_types.len() {
            return Type::default();
        }

        return target_type_fn.param_types[index].clone();
    }

    pub fn infer_call_args(&mut self, call: &mut AstCall, target_type_fn: TypeFn) {
        if !target_type_fn.rest && call.args.len() > target_type_fn.param_types.len() {
            self.errors_push(call.left.start, call.left.end, format!("too many args"));
        }

        if !target_type_fn.rest && call.args.len() < target_type_fn.param_types.len() {
            self.errors_push(call.left.start, call.left.end, format!("not enough args"));
        }

        if call.spread && target_type_fn.rest && call.args.len() != target_type_fn.param_types.len() {
            self.errors_push(
                call.left.start,
                call.left.end,
                format!("spread operator '...' requires a function with rest parameters"),
            );
        }

        let call_args_len = call.args.len();
        for (i, arg) in call.args.iter_mut().enumerate() {
            let is_spread = call.spread && (i == call_args_len - 1);

            let formal_type = self.select_fn_param(i, target_type_fn.clone(), is_spread);

            if let Err(e) = self.infer_right_expr(arg, formal_type) {
                self.errors_push(e.start, e.end, e.message);
            }
        }
    }

    fn self_arg_rewrite(&mut self, type_fn: &TypeFn, self_arg: &mut Expr) -> Result<(), AnalyzerError> {
        let self_param_type = &type_fn.param_types[0];
        if matches!(self_param_type.kind, TypeKind::Ref(_)) {
            if matches!(self_arg.type_.kind, TypeKind::Ptr(_)) {
                return Err(AnalyzerError {
                    start: self_arg.start,
                    end: self_arg.end,
                    message: format!("type mismatch: method requires '{}' receiver, got '{}'", self_param_type, self_arg.type_),
                });
            }

            if matches!(self_arg.type_.kind, TypeKind::Ref(_)) || self_arg.type_.is_heap_impl() {
                return Ok(());
            }

            return Err(AnalyzerError {
                start: self_arg.start,
                end: self_arg.end,
                message: format!("type mismatch: method requires '{}' receiver, got '{}'", self_param_type, self_arg.type_),
            });
        }

        if matches!(self_param_type.kind, TypeKind::Ptr(_)) {
            if matches!(self_arg.type_.kind, TypeKind::Ptr(_) | TypeKind::Ref(_)) {
                return Ok(());
            }

            let mut new_arg = self_arg.clone();
            new_arg.node = AstNode::Unary(ExprOp::La, Box::new(self_arg.clone()));
            new_arg.type_ = Type::ptr_of(self_arg.type_.clone());
            *self_arg = new_arg;
            return Ok(());
        }

        if matches!(self_arg.type_.kind, TypeKind::Ptr(_) | TypeKind::Ref(_)) {
            let mut new_arg = self_arg.clone();
            new_arg.node = AstNode::Unary(ExprOp::Ia, Box::new(self_arg.clone()));
            new_arg.type_ = Type::default();
            *self_arg = new_arg;
        }

        Ok(())
    }

    fn rewrite_generics_ident(&mut self, call: &mut AstCall, target_type: Type, start: usize, end: usize) -> Result<(), AnalyzerError> {
        if !matches!(call.left.node, AstNode::Ident(_, _)) {
            return Ok(());
        }

        let (ident, symbol_id) = match &mut call.left.node {
            AstNode::Ident(ident, symbol_id) => (ident, symbol_id),
            _ => return Ok(()),
        };

        if *symbol_id == 0 {
            return Err(AnalyzerError {
                start,
                end,
                message: format!("symbol '{}' not found", ident),
            });
        }

        let symbol = self.symbol_table.get_symbol(*symbol_id).unwrap();

        // 可能是 local 闭包函数，此时 type 是一个 var
        if symbol.is_local {
            return Ok(());
        }

        // 可能是全局维度的闭包函数
        if !matches!(symbol.kind, SymbolKind::Fn(_)) {
            return Ok(());
        }

        let SymbolKind::Fn(temp_fndef_mutex) = symbol.kind.clone() else {
            return Ok(());
        };

        let temp_fndef = temp_fndef_mutex.lock().unwrap();
        if !temp_fndef.is_generics {
            return Ok(());
        }
        drop(temp_fndef);

        let module_scope_id = symbol.defined_in;

        // 由于存在函数重载，所以需要进行多次匹配找到最合适的 is_tpl 函数, 如果没有重载 temp_fndef 就是 tpl_fndef
        let special_fn = self
            .generics_special_fn(
                (call.args.clone(), call.generics_args.clone(), call.spread.clone()),
                target_type,
                temp_fndef_mutex,
                module_scope_id,
            )
            .map_err(|e| AnalyzerError { start, end, message: e })?;

        let special_fn = special_fn.lock().unwrap();

        // call ident 重写, 从而能够正确的从符号表中检索到 special_fn
        *ident = special_fn.symbol_name.clone();
        *symbol_id = special_fn.symbol_id;

        Ok(())
    }

    fn impl_call_rewrite(&mut self, call: &mut AstCall, target_type: Type, start: usize, end: usize) -> Result<Option<TypeFn>, AnalyzerError> {
        // 获取select表达式
        let AstNode::SelectExpr(mut select_left, key, type_args) = call.left.node.clone() else {
            unreachable!()
        };

        // 获取left的类型(已经在之前推导过)
        let mut left_is_type = false;
        let select_left_type = if let Some(select_left_type) = self.select_left_is_type(&select_left, &type_args, start, end)? {
            left_is_type = true;

            // 类型参数校验：对于 impl 静态方法调用，需要校验类型参数数量
            if let Some(symbol) = self.symbol_table.get_symbol(select_left_type.symbol_id) {
                if let SymbolKind::Type(typedef_mutex) = &symbol.kind {
                    let typedef = typedef_mutex.lock().unwrap();
                    let expected = typedef.params.len();
                    let actual = type_args.as_ref().map_or(0, |args| args.len());
                    if expected != actual {
                        return Err(AnalyzerError {
                            start,
                            end,
                            message: format!("type '{}' expects {} type argument(s), but got {}", typedef.ident, expected, actual),
                        });
                    }
                }
            }

            self.reduction_type(select_left_type)?
        } else {
            self.infer_right_expr(&mut select_left, Type::default())?
        };

        let mut extract_type = select_left_type.clone();
        if matches!(select_left_type.kind, TypeKind::Ref(_) | TypeKind::Ptr(_)) {
            extract_type = match &select_left_type.kind {
                TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => *value_type.clone(),
                _ => unreachable!(),
            };
        }

        // 如果是 struct 类型且 key 是其属性,不需要重写
        if let TypeKind::Struct(_, _, properties) = &extract_type.kind {
            if properties.iter().any(|p| p.name == key) {
                return Ok(None);
            }
        }

        // 比如 int? 转换为 union 时就不存在 ident
        if extract_type.ident.is_empty() {
            return Ok(None);
        }

        // 当 symbol_id 存在时，使用 typedef 的 local ident 进行查找
        // 因为 impl fn 注册时使用的是 typedef 的 local ident（如 bar_t.dump），
        // 而不是 fully qualified ident（如 nature-test.mod.bar_t.dump）
        let mut impl_ident = if extract_type.symbol_id != 0 {
            if let Some(symbol) = self.symbol_table.get_symbol(extract_type.symbol_id) {
                if let SymbolKind::Type(typedef_mutex) = &symbol.kind {
                    let typedef = typedef_mutex.lock().unwrap();
                    typedef.ident.clone()
                } else {
                    extract_type.ident.clone()
                }
            } else {
                extract_type.ident.clone()
            }
        } else {
            extract_type.ident.clone()
        };
        let mut impl_args = extract_type.args.clone();

        // string.len() -> string_len
        // person_t.set_age() -> person_t_set_age()
        // register_global_symbol 中进行类 impl_foramt
        let mut impl_symbol_name = format_impl_ident(impl_ident.clone(), key.clone());

        let (final_symbol_name, symbol_id) = match self.symbol_table.find_symbol_id(&impl_symbol_name, self.symbol_table.global_scope_id) {
            Some(symbol_id) => (impl_symbol_name.clone(), symbol_id),
            None => {
                if extract_type.kind != TypeKind::Ident {
                    // ident to default kind(need args)
                    let mut builtin_type = extract_type.clone();
                    builtin_type.ident = "".to_string();
                    builtin_type.ident_kind = TypeIdentKind::Unknown;
                    builtin_type.args = Vec::new();
                    builtin_type.status = ReductionStatus::Undo;
                    builtin_type = self.reduction_type(builtin_type)?;

                    // builtin 测试
                    if !builtin_type.ident.is_empty() && builtin_type.ident_kind == TypeIdentKind::Builtin {
                        impl_ident = builtin_type.ident.clone();
                        impl_args = builtin_type.args.clone();
                        impl_symbol_name = format_impl_ident(impl_ident.clone(), key.clone());

                        match self.symbol_table.find_symbol_id(&impl_symbol_name, self.symbol_table.global_scope_id) {
                            Some(symbol_id) => {
                                // change self arg 类型
                                match &mut select_left.type_.kind {
                                    TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => {
                                        *value_type = Box::new(builtin_type);
                                    }
                                    _ => {
                                        select_left.type_ = builtin_type;
                                    }
                                }
                                (impl_symbol_name.clone(), symbol_id)
                            }
                            None => {
                                return Err(AnalyzerError {
                                    start,
                                    end,
                                    message: format!("type '{}' no impl fn '{}'", extract_type, key),
                                });
                            }
                        }
                    } else {
                        return Err(AnalyzerError {
                            start,
                            end,
                            message: format!("type '{}' no impl fn '{}'", extract_type, key),
                        });
                    }
                } else {
                    return Err(AnalyzerError {
                        start,
                        end,
                        message: format!("type '{}' no impl fn '{}'", extract_type, key),
                    });
                }
            }
        };

        // 重写 select call 为 ident call
        call.left = Box::new(Expr::ident(call.left.start, call.left.end, final_symbol_name, symbol_id));

        // 对齐编译器：impl type 的泛型参数在前，后接 call 显式方法泛型参数。
        let mut explicit_generics_args = Vec::new();
        if !impl_args.is_empty() || !call.generics_args.is_empty() {
            explicit_generics_args.extend(impl_args.into_iter());
            explicit_generics_args.extend(call.generics_args.clone().into_iter());
        }
        call.generics_args = explicit_generics_args;

        // rewrite_generics_ident: 处理泛型函数的特殊化
        self.rewrite_generics_ident(call, target_type.clone(), start, end)?;

        // 推导左侧表达式类型
        let left_type = self.infer_right_expr(&mut call.left, Type::default())?;

        // 确保是函数类型
        let TypeKind::Fn(type_fn) = left_type.kind else {
            return Err(AnalyzerError {
                start,
                end,
                message: "cannot call non-fn".to_string(),
            });
        };

        let needs_self = match &call.left.node {
            AstNode::Ident(_, symbol_id) => {
                if *symbol_id == 0 {
                    return Err(AnalyzerError {
                        start,
                        end,
                        message: "symbol not found".to_string(),
                    });
                }

                let symbol = self.symbol_table.get_symbol(*symbol_id).ok_or(AnalyzerError {
                    start,
                    end,
                    message: "symbol not found".to_string(),
                })?;
                match &symbol.kind {
                    SymbolKind::Fn(fndef_mutex) => {
                        let fndef = fndef_mutex.lock().unwrap();
                        fndef.self_kind != SelfKind::Null
                    }
                    _ => false,
                }
            }
            _ => false,
        };
        if left_is_type && needs_self {
            return Err(AnalyzerError {
                start,
                end,
                message: format!("method '{}' requires a receiver; use a value instead of a type", key),
            });
        }

        // 构建新的参数列表
        let mut new_args = Vec::new();
        if needs_self {
            let mut self_arg = select_left.clone(); // {'a':1}.del('a') -> map_del({'a':1}, 'a')
            self.self_arg_rewrite(&type_fn, &mut self_arg)?;
            new_args.push(self_arg);
        }
        new_args.extend(call.args.iter().cloned());
        call.args = new_args;

        Ok(Some(*type_fn))
    }

    fn select_left_is_type(&mut self, select_left: &Box<Expr>, type_args: &Option<Vec<Type>>, start: usize, end: usize) -> Result<Option<Type>, AnalyzerError> {
        let AstNode::Ident(_ident, symbol_id) = &select_left.node else {
            return Ok(None);
        };

        if *symbol_id == 0 {
            return Ok(None);
        }

        let symbol = self.symbol_table.get_symbol(*symbol_id).ok_or(AnalyzerError {
            start,
            end,
            message: "symbol not found".to_string(),
        })?;

        let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
            return Ok(None);
        };

        let typedef = typedef_mutex.lock().unwrap();
        let mut t = Type::ident_new(typedef.ident.clone(), TypeIdentKind::Def);
        t.symbol_id = *symbol_id;
        if let Some(args) = type_args.as_ref() {
            t.args = args.clone();
        }

        Ok(Some(t))
    }

    fn impl_static_symbol_exists(&mut self, select_left_type: &Type, key: &String) -> bool {
        if select_left_type.ident.is_empty() {
            return false;
        }

        let impl_symbol_name = format_impl_ident(select_left_type.ident.clone(), key.clone());
        let Ok((_, symbol_id)) = self.find_impl_call_ident(impl_symbol_name, select_left_type.args.clone(), select_left_type) else {
            return false;
        };
        let Some(symbol) = self.symbol_table.get_symbol(symbol_id) else {
            return false;
        };
        match &symbol.kind {
            SymbolKind::Fn(fndef_mutex) => {
                let fndef = fndef_mutex.lock().unwrap();
                fndef.is_static
            }
            _ => false,
        }
    }

    fn try_rewrite_tagged_union_call(&mut self, expr: &mut Box<Expr>, _target_type: &Type) -> Result<bool, AnalyzerError> {
        let AstNode::Call(call) = &mut expr.node else {
            return Ok(false);
        };

        let AstNode::SelectExpr(select_left, key, type_args) = &mut call.left.node else {
            return Ok(false);
        };

        let Some(select_left_type) = self.select_left_is_type(select_left, type_args, expr.start, expr.end)? else {
            return Ok(false);
        };

        // static method priority over tagged union element
        if self.impl_static_symbol_exists(&select_left_type, key) {
            return Ok(false);
        }

        let symbol = self.symbol_table.get_symbol(select_left_type.symbol_id).ok_or(AnalyzerError {
            start: expr.start,
            end: expr.end,
            message: "symbol not found".to_string(),
        })?;
        let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
            return Ok(false);
        };
        let typedef = typedef_mutex.lock().unwrap();
        if !typedef.is_tagged_union {
            return Ok(false);
        }

        let mut new_union_type = Type::ident_new(select_left_type.ident.clone(), TypeIdentKind::TaggedUnion);
        new_union_type.symbol_id = select_left_type.symbol_id;
        new_union_type.start = expr.start;
        new_union_type.end = expr.end;
        if let Some(args) = type_args.as_ref() {
            new_union_type.args = args.clone();
        }

        let arg = if call.args.is_empty() {
            None
        } else if call.args.len() == 1 {
            Some(call.args[0].clone())
        } else {
            Some(Box::new(Expr {
                start: call.args[0].start,
                end: call.args.last().map(|e| e.end).unwrap_or(expr.end),
                type_: Type::default(),
                target_type: Type::default(),
                node: AstNode::TupleNew(call.args.clone()),
            }))
        };

        expr.node = AstNode::TaggedUnionNew(new_union_type, key.clone(), None, arg);
        Ok(true)
    }

    fn try_rewrite_tagged_union_select(&mut self, expr: &mut Box<Expr>, _target_type: &Type) -> Result<bool, AnalyzerError> {
        let AstNode::SelectExpr(select_left, key, type_args) = &mut expr.node else {
            return Ok(false);
        };

        let Some(select_left_type) = self.select_left_is_type(select_left, type_args, expr.start, expr.end)? else {
            return Ok(false);
        };

        // static method priority over tagged union element
        if self.impl_static_symbol_exists(&select_left_type, key) {
            return Ok(false);
        }

        let symbol = self.symbol_table.get_symbol(select_left_type.symbol_id).ok_or(AnalyzerError {
            start: expr.start,
            end: expr.end,
            message: "symbol not found".to_string(),
        })?;
        let SymbolKind::Type(typedef_mutex) = &symbol.kind else {
            return Ok(false);
        };
        let typedef = typedef_mutex.lock().unwrap();
        if !typedef.is_tagged_union {
            return Ok(false);
        }

        let mut new_union_type = Type::ident_new(select_left_type.ident.clone(), TypeIdentKind::TaggedUnion);
        new_union_type.symbol_id = select_left_type.symbol_id;
        new_union_type.start = expr.start;
        new_union_type.end = expr.end;
        if let Some(args) = type_args.as_ref() {
            new_union_type.args = args.clone();
        }

        expr.node = AstNode::TaggedUnionNew(new_union_type, key.clone(), None, None);
        Ok(true)
    }

    pub fn infer_call_left(&mut self, call: &mut AstCall, target_type: Type, start: usize, end: usize) -> Result<TypeKind, AnalyzerError> {
        // --------------------------------------------interface call handle----------------------------------------------------------
        if let AstNode::SelectExpr(select_left, key, _) = &mut call.left.node {
            let mut skip_interface = false;
            if let AstNode::Ident(_, symbol_id) = &select_left.node {
                if *symbol_id != 0 {
                    if let Some(symbol) = self.symbol_table.get_symbol(*symbol_id) {
                        if matches!(symbol.kind, SymbolKind::Type(_)) {
                            skip_interface = true;
                        }
                    }
                }
            }

            if !skip_interface {
                let select_left_type = self.infer_right_expr(select_left, Type::default())?;

                if let TypeKind::Interface(elements) = &select_left_type.kind {
                    // 在接口中查找对应的方法
                    for element in elements {
                        if let TypeKind::Fn(fn_type) = &element.kind {
                            if fn_type.name == *key {
                                return Ok(element.kind.clone());
                            }
                        }
                    }

                    return Err(AnalyzerError {
                        start,
                        end,
                        message: format!("interface '{}' not declare '{}' fn", select_left_type.ident, key),
                    });
                }
            }
        }

        // --------------------------------------------impl type handle----------------------------------------------------------
        if let AstNode::SelectExpr(_, _, _) = &mut call.left.node {
            if let Some(type_fn) = self.impl_call_rewrite(call, target_type.clone(), start, end)? {
                return Ok(TypeKind::Fn(Box::new(type_fn)));
            }
        }

        // --------------------------------------------generics handle----------------------------------------------------------
        self.rewrite_generics_ident(call, target_type.clone(), start, end)?;

        // 推导左侧表达式类型
        let left_type = self.infer_right_expr(&mut call.left, Type::default())?;

        // 确保是函数类型
        if !matches!(left_type.kind, TypeKind::Fn(..)) {
            return Err(AnalyzerError {
                start,
                end,
                message: "cannot call non-fn".to_string(),
            });
        }

        Ok(left_type.kind)
    }

    pub fn infer_call(&mut self, call: &mut AstCall, target_type: Type, start: usize, end: usize, check_errable: bool) -> Result<Type, AnalyzerError> {
        let fn_kind: TypeKind = self.infer_call_left(call, target_type, start, end)?;

        let TypeKind::Fn(type_fn) = fn_kind else { unreachable!() };
        self.infer_call_args(call, *type_fn.clone());
        call.return_type = type_fn.return_type.clone();

        {
            let current_fn = self.current_fn_mutex.lock().unwrap();
            if current_fn.is_fx && !type_fn.fx {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: format!(
                        "calling fn `{}` from fx `{}` is not allowed.",
                        if type_fn.name.is_empty() {
                            "lambda".to_string()
                        } else {
                            type_fn.name.clone()
                        },
                        current_fn.fn_name
                    ),
                });
            }
        }

        if type_fn.errable && check_errable {
            // 当前 fn 必须允许 is_errable 或者当前位于 be_caught 中
            let current_fn = self.current_fn_mutex.lock().unwrap();
            if self.be_caught == 0 && !current_fn.is_errable {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: format!(
                        "calling an errable! fn `{}` requires the current `fn {}` errable! as well or be caught.",
                        if type_fn.name.is_empty() { "lambda".to_string() } else { type_fn.name },
                        current_fn.fn_name
                    ),
                });
            }
        }

        return Ok(type_fn.return_type.clone());
    }

    pub fn infer_for_iterator(
        &mut self,
        iterate: &mut Box<Expr>,
        first: &mut Arc<Mutex<VarDeclExpr>>,
        second: &mut Option<Arc<Mutex<VarDeclExpr>>>,
        body: &mut AstBody,
    ) {
        let iterate_type = match self.infer_right_expr(iterate, Type::default()) {
            Ok(iterate_type) => iterate_type,
            Err(e) => {
                self.errors_push(e.start, e.end, e.message);
                return;
            }
        };

        // 检查迭代类型
        match iterate_type.kind {
            TypeKind::Map(..) | TypeKind::Vec(..) | TypeKind::String | TypeKind::Chan(..) => {}
            _ => {
                self.errors_push(
                    iterate.start,
                    iterate.end,
                    format!("for in iterate type must be map/list/string/chan, actual={}", iterate_type),
                );
                return;
            }
        }

        // rewrite var declarations
        self.rewrite_var_decl(first.clone());
        if let Some(second) = second {
            self.rewrite_var_decl(second.clone());
        }

        // 检查 chan 类型的特殊情况
        if matches!(iterate_type.kind, TypeKind::Chan(..)) && second.is_some() {
            self.errors_push(iterate.start, iterate.end, "for chan only have one receive parameter".to_string());
            return;
        }

        // 为变量设置类型
        {
            let mut first_decl = first.lock().unwrap();
            match &iterate_type.kind {
                TypeKind::Map(key_type, _) => {
                    first_decl.type_ = *key_type.clone();
                }
                TypeKind::Chan(chan_type) => {
                    first_decl.type_ = *chan_type.clone();
                }
                TypeKind::String => {
                    if second.is_none() {
                        first_decl.type_ = Type::new(TypeKind::Uint8);
                    } else {
                        first_decl.type_ = Type::new(TypeKind::Int);
                    }
                }
                TypeKind::Vec(element_type) => {
                    if second.is_none() {
                        first_decl.type_ = *element_type.clone();
                    } else {
                        first_decl.type_ = Type::new(TypeKind::Int);
                    }
                }
                _ => unreachable!(),
            }
        }

        // 处理第二个变量的类型
        if let Some(second) = second {
            let mut second_decl = second.lock().unwrap();
            match &iterate_type.kind {
                TypeKind::Map(_, value_type) => {
                    second_decl.type_ = *value_type.clone();
                }
                TypeKind::String => {
                    second_decl.type_ = Type::new(TypeKind::Uint8);
                }
                TypeKind::Vec(element_type) => {
                    second_decl.type_ = *element_type.clone();
                }
                _ => unreachable!(),
            }
        }

        // 处理循环体
        self.in_for_count += 1;
        self.infer_body(body);
        self.in_for_count -= 1;
    }

    pub fn interface_throwable(&mut self) -> Type {
        let mut result = Type::ident_new("throwable".to_string(), TypeIdentKind::Interface);

        // find symbol id from global
        let symbol_id = self.symbol_table.find_symbol_id("throwable", self.symbol_table.global_scope_id).unwrap();
        result.symbol_id = symbol_id;
        return self.reduction_type(result).unwrap();
    }

    pub fn infer_stmt(&mut self, stmt: &mut Box<Stmt>) -> Result<(), AnalyzerError> {
        match &mut stmt.node {
            AstNode::Fake(expr) => {
                self.infer_right_expr(expr, Type::default())?;
                if matches!(expr.node, AstNode::Match(..)) && expr.type_.kind.is_unknown() {
                    expr.type_ = Type::new(TypeKind::Void);
                    expr.target_type = Type::new(TypeKind::Void);
                }
            }
            AstNode::VarDef(var_decl_mutex, expr) => {
                self.infer_vardef(var_decl_mutex, expr)?;
            }
            AstNode::VarTupleDestr(elements, right) => {
                debug_assert!((*elements).len() == elements.len());

                let right_type = self.infer_right_expr(right, Type::default())?;

                if !matches!(right_type.kind, TypeKind::Tuple(..)) {
                    return Err(AnalyzerError {
                        start: right.start,
                        end: right.end,
                        message: format!("cannot assign {} to tuple", right_type),
                    });
                }

                self.infer_var_tuple_destr(elements, right_type, stmt.start, stmt.end)?;
            }
            AstNode::Assign(left, right) => match self.infer_left_expr(left) {
                Ok(left_type) => {
                    if left_type.ident_kind != TypeIdentKind::GenericsParam && left_type.kind == TypeKind::Void {
                        return Err(AnalyzerError {
                            start: left.start,
                            end: left.end,
                            message: format!("cannot assign to void"),
                        });
                    }

                    self.infer_right_expr(right, left_type)?;
                }
                Err(e) => {
                    return Err(e);
                }
            },
            AstNode::FnDef(_) => {}
            AstNode::Call(call) => {
                self.infer_call(call, Type::new(TypeKind::Void), stmt.start, stmt.end, true)?;
            }
            AstNode::TryCatch(try_body, catch_err_mutex, catch_body) => {
                self.infer_try_catch(try_body, catch_err_mutex, catch_body)?;
            }
            AstNode::Catch(try_expr, catch_err_mutex, catch_body) => {
                self.infer_catch(try_expr, catch_err_mutex, catch_body)?;
            }
            AstNode::Select(cases, _has_default, _send_count, _recv_count) => {
                for case in cases.iter_mut() {
                    if let Some(on_call) = &mut case.on_call {
                        if let Err(e) = self.infer_call(on_call, Type::default(), case.start, case.end, true) {
                            self.errors_push(e.start, e.end, e.message);
                        }
                    }

                    if let Some(recv_var_mutex) = &case.recv_var {
                        {
                            // 存在 recv_var 必定存在 on_call
                            let Some(on_call) = &case.on_call else { unreachable!() };
                            let mut recv_var = recv_var_mutex.lock().unwrap();
                            recv_var.type_ = on_call.return_type.clone();

                            if matches!(recv_var.type_.kind, TypeKind::Unknown | TypeKind::Void | TypeKind::Null) {
                                self.errors_push(
                                    recv_var.symbol_start,
                                    recv_var.symbol_end,
                                    format!("variable declaration cannot use type {}", recv_var.type_),
                                );
                            }
                        }

                        self.rewrite_var_decl(recv_var_mutex.clone());
                    }

                    self.infer_body(&mut case.handle_body);
                }
            }
            AstNode::If(cond, consequent, alternate) => {
                if let Err(e) = self.infer_right_expr(cond, Type::new(TypeKind::Bool)) {
                    self.errors_push(e.start, e.end, e.message);
                }

                self.infer_body(consequent);
                self.infer_body(alternate);
            }
            AstNode::ForCond(condition, body) => {
                if let Err(e) = self.infer_right_expr(condition, Type::new(TypeKind::Bool)) {
                    self.errors_push(e.start, e.end, e.message);
                }

                self.in_for_count += 1;
                self.infer_body(body);
                self.in_for_count -= 1;
            }
            AstNode::ForIterator(iterate, first, second, body) => {
                self.infer_for_iterator(iterate, first, second, body);
            }
            AstNode::Break | AstNode::Continue => {
                if self.in_for_count == 0 {
                    return Err(AnalyzerError {
                        message: "break or continue must in for body".to_string(),
                        start: stmt.start,
                        end: stmt.end,
                    });
                }
            }
            AstNode::ForTradition(init, condition, update, body) => {
                if let Err(e) = self.infer_stmt(init) {
                    self.errors_push(e.start, e.end, e.message);
                }

                if let Err(e) = self.infer_right_expr(condition, Type::new(TypeKind::Bool)) {
                    self.errors_push(e.start, e.end, e.message);
                }

                if let Err(e) = self.infer_stmt(update) {
                    self.errors_push(e.start, e.end, e.message);
                }

                self.in_for_count += 1;
                self.infer_body(body);
                self.in_for_count -= 1;
            }
            AstNode::Throw(expr) => {
                {
                    let (is_errable, fn_name, return_type) = {
                        let current_fn = self.current_fn_mutex.lock().unwrap();
                        (current_fn.is_errable, current_fn.fn_name.clone(), current_fn.return_type.clone())
                    };

                    if !is_errable {
                        //  "can't use throw stmt in a fn without an errable! declaration. example: fn %s(...):%s!",
                        self.errors_push(
                            expr.start,
                            expr.end,
                            format!(
                                "can't use throw stmt in a fn without an errable! declaration. example: fn {}(...):{}!",
                                fn_name, return_type
                            ),
                        );
                    }
                    let target_type = self.interface_throwable();
                    self.infer_right_expr(expr, target_type)?;
                }
            }
            AstNode::Return(expr_option) => {
                let target_type = {
                    let current_fn = self.current_fn_mutex.lock().unwrap();
                    current_fn.return_type.clone()
                };

                if let Some(expr) = expr_option {
                    if let Err(e) = self.infer_right_expr(expr, target_type) {
                        self.errors_push(e.start, e.end, e.message);
                    }
                } else {
                    // target_type kind mut void
                    if !matches!(target_type.kind, TypeKind::Void) {
                        self.errors_push(stmt.start, stmt.end, format!("fn expect return type {}, but got void", target_type));
                    }
                }
            }
            AstNode::Ret(expr) => {
                let target_type = self.ret_target_types.last().unwrap().clone();

                // get break target type by break_target_types top
                if target_type.kind != TypeKind::Void {
                    let new_handle_type = self.infer_right_expr(expr, target_type.clone())?;
                    if target_type.kind.is_unknown() {
                        let ptr = self.ret_target_types.last_mut().unwrap();
                        *ptr = new_handle_type;
                    }
                } else {
                    self.infer_right_expr(expr, Type::default())?;
                }
            }
            AstNode::Typedef(type_alias_mutex) => {
                self.rewrite_typedef(type_alias_mutex);
            }
            _ => {}
        }

        Ok(())
    }

    pub fn infer_body(&mut self, body: &mut AstBody) {
        for stmt in &mut body.stmts {
            if let Err(e) = self.infer_stmt(stmt) {
                self.errors_push(e.start, e.end, e.message);
            }
        }
    }

    pub fn type_confirm(&mut self, t: &Type) -> bool {
        if t.err {
            return false;
        }

        // 检查基本类型是否为未知类型
        if matches!(t.kind, TypeKind::Unknown) {
            return false;
        }

        // 检查容器类型的元素类型
        match &t.kind {
            // 检查向量类型的元素类型
            TypeKind::Vec(element_type) => {
                if matches!(element_type.kind, TypeKind::Unknown) {
                    return false;
                }
            }

            // 检查映射类型的键值类型
            TypeKind::Map(key_type, value_type) => {
                if matches!(key_type.kind, TypeKind::Unknown) || matches!(value_type.kind, TypeKind::Unknown) {
                    return false;
                }
            }

            // 检查集合类型的元素类型
            TypeKind::Set(element_type) => {
                if matches!(element_type.kind, TypeKind::Unknown) {
                    return false;
                }
            }

            // 检查元组类型的所有元素类型
            TypeKind::Tuple(elements, _) => {
                for element_type in elements {
                    if matches!(element_type.kind, TypeKind::Unknown) {
                        return false;
                    }
                }
            }

            // 其他基本类型都认为是已确认的
            _ => {}
        }

        true
    }

    pub fn type_compare_no_ident(&mut self, mut dst: Type, mut src: Type) -> bool {
        dst.ident = "".to_string();
        src.ident = "".to_string();
        src.ident_kind = TypeIdentKind::Unknown;
        dst.ident_kind = TypeIdentKind::Unknown;

        return self.type_compare(&dst, &src);
    }

    pub fn type_compare(&mut self, dst: &Type, src: &Type) -> bool {
        let mut visited: HashSet<String> = HashSet::new();
        self.type_compare_visited(dst, src, &mut visited)
    }

    fn type_compare_ident_args_visited(&mut self, dst_args: &Vec<Type>, src_args: &Vec<Type>, visited: &mut HashSet<String>) -> bool {
        if dst_args.len() != src_args.len() {
            return false;
        }

        for (dst_arg, src_arg) in dst_args.iter().zip(src_args.iter()) {
            if !self.type_compare_visited(dst_arg, src_arg, visited) {
                return false;
            }
        }

        true
    }

    pub fn type_compare_visited(&mut self, dst: &Type, src: &Type, visited: &mut HashSet<String>) -> bool {
        let dst = dst.clone();
        if dst.err || src.err {
            return false;
        }

        debug_assert!(!Type::ident_is_generics_param(&dst));
        debug_assert!(!Type::ident_is_generics_param(src));

        // 检查类型状态
        if dst.status != ReductionStatus::Done {
            return false;
        }
        if src.status != ReductionStatus::Done {
            return false;
        }
        if matches!(dst.kind, TypeKind::Unknown) || matches!(src.kind, TypeKind::Unknown) {
            return false;
        }

        if dst.ident_kind == TypeIdentKind::Builtin && dst.ident == "all_t".to_string() {
            return true;
        }

        // fn_t 可以匹配所有函数类型
        if dst.ident_kind == TypeIdentKind::Builtin && dst.ident == "fn_t" && matches!(src.kind, TypeKind::Fn(..)) {
            return true;
        }

        if dst.ident_kind == TypeIdentKind::Builtin && dst.ident == "integer_t" && Type::is_integer(&src.kind) {
            return true;
        }

        if dst.ident_kind == TypeIdentKind::Builtin && dst.ident == "floater_t" && Type::is_float(&src.kind) {
            return true;
        }

        // ptr<t> 可以赋值为 null 以及 ref<t>
        if let TypeKind::Ptr(dst_value_type) = &dst.kind {
            match &src.kind {
                TypeKind::Null => return true,
                TypeKind::Ref(src_value_type) => {
                    return self.type_compare_visited(dst_value_type, src_value_type, visited);
                }
                _ => {}
            }
        }

        // TYPE_IDENT_DEF 采用名义类型比较：ident + args
        let dst_nominal = dst.ident_kind == TypeIdentKind::Def && !matches!(dst.kind, TypeKind::Union(..));
        let src_nominal = src.ident_kind == TypeIdentKind::Def && !matches!(src.kind, TypeKind::Union(..));
        if dst_nominal || src_nominal {
            if dst.ident.is_empty() || src.ident.is_empty() {
                return false;
            }

            if dst.ident != src.ident {
                return false;
            }

            if !self.type_compare_ident_args_visited(&dst.args, &src.args, visited) {
                return false;
            }

            return true;
        }

        // ident 递归打断
        if dst.ident_kind == TypeIdentKind::Def {
            if visited.contains(&dst.ident) {
                return true;
            }
            visited.insert(dst.ident.clone());
        }

        // 如果类型不同则返回false
        if dst.kind != src.kind {
            return false;
        }

        // 根据不同类型进行比较
        match (&dst.kind, &src.kind) {
            (TypeKind::Union(left_any, _, left_types), TypeKind::Union(right_any, _, right_types)) => {
                if *left_any {
                    return true;
                }
                return self.type_union_compare(&(*left_any, left_types.clone()), &(*right_any, right_types.clone()), visited);
            }

            (TypeKind::TaggedUnion(_, left_elements), TypeKind::TaggedUnion(_, right_elements)) => {
                if left_elements.len() != right_elements.len() {
                    return false;
                }
                for (left_ele, right_ele) in left_elements.iter().zip(right_elements.iter()) {
                    // 比较 tag 名称
                    if left_ele.tag != right_ele.tag {
                        return false;
                    }
                    // 比较元素类型
                    if !self.type_compare_visited(&left_ele.type_, &right_ele.type_, visited) {
                        return false;
                    }
                }
                return true;
            }

            (TypeKind::Map(left_key, left_value), TypeKind::Map(right_key, right_value)) => {
                if !self.type_compare_visited(left_key, right_key, visited) {
                    return false;
                }
                if !self.type_compare_visited(left_value, right_value, visited) {
                    return false;
                }

                return true;
            }

            (TypeKind::Set(left_element), TypeKind::Set(right_element)) => self.type_compare_visited(left_element, right_element, visited),

            (TypeKind::Chan(left_element), TypeKind::Chan(right_element)) => self.type_compare_visited(left_element, right_element, visited),

            (TypeKind::Vec(left_element), TypeKind::Vec(right_element)) => self.type_compare_visited(left_element, right_element, visited),

            (TypeKind::Arr(_, left_len, left_element), TypeKind::Arr(_, right_len, right_element)) => {
                left_len == right_len && self.type_compare_visited(left_element, right_element, visited)
            }

            (TypeKind::Tuple(left_elements, _), TypeKind::Tuple(right_elements, _)) => {
                if left_elements.len() != right_elements.len() {
                    return false;
                }
                left_elements
                    .iter()
                    .zip(right_elements.iter())
                    .all(|(left, right)| self.type_compare_visited(left, right, visited))
            }

            (TypeKind::Fn(left_fn), TypeKind::Fn(right_fn)) => {
                if !self.type_compare_visited(&left_fn.return_type, &right_fn.return_type, visited)
                    || left_fn.param_types.len() != right_fn.param_types.len()
                    || left_fn.rest != right_fn.rest
                    || left_fn.errable != right_fn.errable
                    || left_fn.fx != right_fn.fx
                {
                    return false;
                }

                left_fn
                    .param_types
                    .iter()
                    .zip(right_fn.param_types.iter())
                    .all(|(left, right)| self.type_compare_visited(left, right, visited))
            }

            (TypeKind::Struct(_, _, left_props), TypeKind::Struct(_, _, right_props)) => {
                if left_props.len() != right_props.len() {
                    return false;
                }
                left_props
                    .iter()
                    .zip(right_props.iter())
                    .all(|(left, right)| left.name == right.name && self.type_compare_visited(&left.type_, &right.type_, visited))
            }

            (TypeKind::Ref(left_value), TypeKind::Ref(right_value)) | (TypeKind::Ptr(left_value), TypeKind::Ptr(right_value)) => {
                self.type_compare_visited(left_value, right_value, visited)
            }

            // 其他基本类型直接返回true
            _ => true,
        }
    }

    pub fn type_generics(&mut self, dst: &Type, src: &Type, generics_param_table: &mut HashMap<String, Type>) -> bool {
        let dst = dst.clone();
        if dst.err || src.err {
            return false;
        }

        if matches!(dst.kind, TypeKind::Unknown) || matches!(src.kind, TypeKind::Unknown) {
            return false;
        }

        // 处理泛型参数
        if Type::ident_is_generics_param(&dst) {
            debug_assert!(src.status == ReductionStatus::Done);

            // let generics_param_table = generics_param_table.as_mut().unwrap();
            let param_ident = dst.ident.clone();

            if let Some(_target_type) = generics_param_table.get(&param_ident) {
                // dst = _target_type.clone();
            } else {
                let target_type = src.clone();
                generics_param_table.insert(param_ident.clone(), target_type.clone());
                // dst = _target_type;
            }

            return true;
        }

        // 如果类型不同则返回false
        if dst.kind != src.kind {
            return false;
        }

        // 根据不同类型进行比较
        match (&dst.kind, &src.kind) {
            (TypeKind::Union(left_any, _, left_types), TypeKind::Union(right_any, _, right_types)) => {
                if left_any != right_any {
                    return false;
                }

                if left_types.len() != right_types.len() {
                    return false;
                }

                for i in 0..left_types.len() {
                    let dst_item = left_types[i].clone();
                    let src_item = right_types[i].clone();

                    if !self.type_generics(&dst_item, &src_item, generics_param_table) {
                        return false;
                    }
                }

                return true;
            }

            (TypeKind::Map(left_key, left_value), TypeKind::Map(right_key, right_value)) => {
                if !self.type_generics(left_key, right_key, generics_param_table) {
                    return false;
                }
                if !self.type_generics(left_value, right_value, generics_param_table) {
                    return false;
                }

                return true;
            }

            (TypeKind::Set(left_element), TypeKind::Set(right_element)) => self.type_generics(left_element, right_element, generics_param_table),

            (TypeKind::Chan(left_element), TypeKind::Chan(right_element)) => self.type_generics(left_element, right_element, generics_param_table),

            (TypeKind::Vec(left_element), TypeKind::Vec(right_element)) => self.type_generics(left_element, right_element, generics_param_table),

            (TypeKind::Arr(_, left_len, left_element), TypeKind::Arr(_, right_len, right_element)) => {
                left_len == right_len && self.type_generics(left_element, right_element, generics_param_table)
            }

            (TypeKind::Tuple(left_elements, _), TypeKind::Tuple(right_elements, _)) => {
                if left_elements.len() != right_elements.len() {
                    return false;
                }
                left_elements
                    .iter()
                    .zip(right_elements.iter())
                    .all(|(left, right)| self.type_generics(left, right, generics_param_table))
            }

            (TypeKind::Fn(left_fn), TypeKind::Fn(right_fn)) => {
                if !self.type_generics(&left_fn.return_type, &right_fn.return_type, generics_param_table)
                    || left_fn.param_types.len() != right_fn.param_types.len()
                    || left_fn.rest != right_fn.rest
                    || left_fn.errable != right_fn.errable
                    || left_fn.fx != right_fn.fx
                {
                    return false;
                }

                left_fn
                    .param_types
                    .iter()
                    .zip(right_fn.param_types.iter())
                    .all(|(left, right)| self.type_generics(left, right, generics_param_table))
            }

            (TypeKind::Struct(_, _, left_props), TypeKind::Struct(_, _, right_props)) => {
                if left_props.len() != right_props.len() {
                    return false;
                }
                left_props
                    .iter()
                    .zip(right_props.iter())
                    .all(|(left, right)| left.name == right.name && self.type_generics(&left.type_, &right.type_, generics_param_table))
            }

            (TypeKind::Ref(left_value), TypeKind::Ref(right_value)) | (TypeKind::Ptr(left_value), TypeKind::Ptr(right_value)) => {
                self.type_generics(left_value, right_value, generics_param_table)
            }

            // 其他基本类型直接返回true
            _ => true,
        }
    }

    pub fn infer_fn_decl(&mut self, fndef_mutex: Arc<Mutex<AstFnDef>>, target_type: Type) -> Result<Type, AnalyzerError> {
        let fndef = {
            // 如果已经完成类型推导，直接返回
            let mut fndef = fndef_mutex.lock().unwrap();
            if fndef.type_.kind.is_exist() && fndef.type_.status == ReductionStatus::Done {
                if target_type.kind.is_exist() {
                    if self.type_compare_no_ident(target_type.clone(), fndef.type_.clone()) {
                        fndef.type_.ident = target_type.ident.clone();
                        fndef.type_.ident_kind = target_type.ident_kind.clone();
                        fndef.type_.args = target_type.args.clone();
                    }
                }

                return Ok(fndef.type_.clone());
            }

            fndef.clone()
        };

        if fndef.is_generics {
            if self.generics_args_stack.is_empty() {
                return Err(AnalyzerError {
                    start: 0,
                    end: 0,
                    message: format!("cannot infer generics fn `{}`", fndef.fn_name),
                });
            }

            // arg table 必须存在，且已经推导
            let table = self.generics_args_stack.first().unwrap();
            if let Some(generics_params) = &fndef.generics_params {
                for param in generics_params {
                    let _arg_type = match table.get(&param.ident) {
                        Some(arg_type) => arg_type,
                        None => {
                            return Err(AnalyzerError {
                                start: 0,
                                end: 0,
                                message: format!("cannot infer generics fn {}", fndef.fn_name),
                            })
                        }
                    };
                }
            }
        }

        // 对返回类型进行还原
        let return_type = match self.reduction_type(fndef.return_type) {
            Ok(return_type) => {
                // mutex
                {
                    let mut temp_fn = fndef_mutex.lock().unwrap();
                    temp_fn.return_type = return_type.clone();
                }

                return_type
            }
            Err(e) => {
                return Err(e);
            }
        };

        // 处理参数类型
        let mut param_types = Vec::new();

        for (i, param_mutex) in fndef.params.iter().enumerate() {
            let param_type = {
                let temp = param_mutex.lock().unwrap();

                // debug!(
                //     "[infer_fn_decl] fn {} handle param {}, staus {}, ident {}, have err {}",
                //     fndef.symbol_name, temp.ident, temp.type_.status, temp.type_.ident, temp.type_.err
                // );

                temp.type_.clone()
            };

            // 对参数类型进行还原
            let mut param_type = self.reduction_type(param_type)?;
            if param_type.kind == TypeKind::Ident {
                return Err(AnalyzerError {
                    start: 0,
                    end: 0,
                    message: format!("cannot reduction param {}", param_type),
                });
            }

            // 为什么要在这里进行 ptr of, 只有在 infer 之后才能确定 alias 的具体类型，从而进一步判断是否需要 ptrof
            // is_impl 并且是第一个参数时，根据 self_kind 处理
            if fndef.is_impl && !fndef.is_static && fndef.self_kind != SelfKind::Null && i == 0 {
                if param_type.is_stack_impl() {
                    match fndef.self_kind {
                        SelfKind::SelfRefT => {
                            param_type = Type::ref_of(param_type);
                        }
                        SelfKind::SelfPtrT => {
                            param_type = Type::ptr_of(param_type);
                        }
                        SelfKind::SelfT | SelfKind::Null => {
                            // 值类型传递，不需要转换
                        }
                    }
                } else {
                    // 堆分配类型(vec/map/set/chan等)有隐式指针接收器
                    // 不需要转换，但必须是 SelfPtrT
                    if fndef.self_kind != SelfKind::SelfRefT && fndef.self_kind != SelfKind::Null {
                        // 可以在这里添加警告或错误，但暂时只是保持原样
                    }
                }
            }

            {
                let mut temp = param_mutex.lock().unwrap();
                temp.type_ = param_type.clone();
            }

            param_types.push(param_type);
        }

        // type done
        let mut result = Type::new(TypeKind::Fn(Box::new(TypeFn {
            return_type,
            name: fndef.fn_name.clone(),
            tpl: fndef.is_tpl,
            errable: fndef.is_errable,
            rest: fndef.rest_param,
            fx: fndef.is_fx,
            param_types,
        })));

        if target_type.kind.is_exist() {
            if self.type_compare_no_ident(target_type.clone(), result.clone()) {
                result.ident = target_type.ident.clone();
                result.ident_kind = target_type.ident_kind.clone();
                result.args = target_type.args.clone();
            }
        }

        {
            let mut temp_fn = fndef_mutex.lock().unwrap();
            temp_fn.type_ = result.clone();
        }

        Ok(result)
    }

    // 新增一个方法来处理模块内的操作
    // fn current_fn_module<F, R>(&mut self, f: F) -> R
    // where
    //     F: FnOnce(&mut Module) -> R,
    // {
    //     let module_index = self.current_fn_mutex.lock().unwrap().module_index;
    //     let mut modules = self.module_db.lock().unwrap();
    //     let module = &mut modules[module_index];
    //     f(module)
    // }

    /**
     * rewrite 的核心目的就是如果当前 type 定义在泛型函数作用域中，那么需要对其进行泛型的 special
     */
    pub fn rewrite_typedef(&mut self, type_alias_mutex: &Arc<Mutex<TypedefStmt>>) {
        // 如果不存在 params_hash 表示当前 fndef 不存在基于泛型的重写，所以 alias 也不需要进行重写
        if let Some(hash) = self.current_fn_mutex.lock().unwrap().generics_args_hash {
            // alias.ident@hash
            let mut type_alias = type_alias_mutex.lock().unwrap();

            debug_assert!(type_alias.symbol_id > 0);
            let original_symbol_defined_in = self.symbol_table.find_scope_id(type_alias.symbol_id);

            type_alias.ident = format_generics_ident(type_alias.ident.clone(), hash);

            match self.symbol_table.define_symbol_in_scope(
                type_alias.ident.clone(),
                SymbolKind::Type(type_alias_mutex.clone()),
                type_alias.symbol_start,
                original_symbol_defined_in,
            ) {
                Ok(symbol_id) => {
                    let original_symbol = self.symbol_table.get_symbol(type_alias.symbol_id).unwrap();
                    original_symbol.generics_id_map.insert(type_alias.ident.clone(), symbol_id);

                    type_alias.symbol_id = symbol_id;
                }
                Err(_e) => {}
            }
        }
    }

    pub fn rewrite_var_decl(&mut self, var_decl_mutex: Arc<Mutex<VarDeclExpr>>) {
        if let Some(hash) = self.current_fn_mutex.lock().unwrap().generics_args_hash {
            let mut var_decl = var_decl_mutex.lock().unwrap();
            if var_decl.symbol_id == 0 {
                return;
            }

            if var_decl.ident.contains('#') {
                return;
            }

            // old symbol
            let original_symbol_defined_in = self.symbol_table.find_scope_id(var_decl.symbol_id);

            // ident@arg_hash
            var_decl.ident = format_generics_ident(var_decl.ident.clone(), hash);

            // TODO redefine symbol 需要自定义 scope id, 不然全部定义到 global 中了

            // symbol register
            match self.symbol_table.define_symbol_in_scope(
                var_decl.ident.clone(),
                SymbolKind::Var(var_decl_mutex.clone()),
                var_decl.symbol_start,
                original_symbol_defined_in,
            ) {
                Ok(symbol_id) => {
                    // 基于 old symbol id 获取 symbol 并建立 generics_id_map 映射
                    let original_symbol = self.symbol_table.get_symbol(var_decl.symbol_id).unwrap();
                    original_symbol.generics_id_map.insert(var_decl.ident.clone(), symbol_id);

                    var_decl.symbol_id = symbol_id;
                }
                Err(_e) => {}
            }
        }
    }

    pub fn infer_var_decl(&mut self, var_decl_mutex: Arc<Mutex<VarDeclExpr>>) -> Result<(), AnalyzerError> {
        let mut var_decl = var_decl_mutex.lock().unwrap();
        var_decl.type_ = self.reduction_type(var_decl.type_.clone())?;

        if matches!(var_decl.type_.kind, TypeKind::Unknown | TypeKind::Void | TypeKind::Null) {
            return Err(AnalyzerError {
                message: format!("variable declaration cannot use type {}", var_decl.type_),
                start: var_decl.symbol_start,
                end: var_decl.symbol_end,
            });
        }

        Ok(())
    }

    pub fn infer_fndef(&mut self, fndef_mutex: Arc<Mutex<AstFnDef>>) {
        self.current_fn_mutex = fndef_mutex;

        let params = {
            let fndef: std::sync::MutexGuard<'_, AstFnDef> = self.current_fn_mutex.lock().unwrap();

            // debug!(
            //     "[infer_fndef] symbol_name is -> {}, hash is {}",
            //     fndef.symbol_name,
            //     fndef.generics_args_hash.unwrap_or(0)
            // );

            if fndef.type_.status != ReductionStatus::Done {
                return;
            }
            fndef.params.clone()
        };

        // rewrite formal ident
        for var_decl_mutex in params {
            self.rewrite_var_decl(var_decl_mutex);
        }

        // handle body
        {
            // handle body - 修改这部分
            let mut body = {
                let mut fndef = self.current_fn_mutex.lock().unwrap();
                std::mem::take(&mut fndef.body) // 临时取出 body 的所有权
            };

            self.infer_body(&mut body);

            {
                let mut fndef = self.current_fn_mutex.lock().unwrap();
                fndef.body = body;
            }
        }
    }

    fn type_union_compare(&mut self, left: &(bool, Vec<Type>), right: &(bool, Vec<Type>), visited: &mut HashSet<String>) -> bool {
        if left.0 == true {
            return true;
        }

        if right.0 && !left.0 {
            return false;
        }

        for right_type in right.1.iter() {
            if left
                .1
                .iter()
                .find(|left_type| self.type_compare_visited(left_type, right_type, visited))
                .is_none()
            {
                return false;
            }
        }

        return true;
    }

    /**
     * 为了能够和 impl 中声明的 fn 进行 compare, 需要将 fn 将 self 参数暂时去除, 并且不改变 fndef 中的 ident/return_type/type 等
     */
    fn infer_impl_fn_decl(&mut self, fndef: &AstFnDef) -> Result<Type, AnalyzerError> {
        if fndef.impl_type.kind.is_unknown() {
            return Err(AnalyzerError {
                start: 0,
                end: 0,
                message: "cannot infer function without interface".to_string(),
            });
        }

        let mut type_fn = TypeFn {
            name: fndef.fn_name.clone(),
            tpl: fndef.is_tpl,
            errable: fndef.is_errable,
            rest: fndef.rest_param,
            fx: fndef.is_fx,
            param_types: Vec::new(),
            return_type: self.reduction_type(fndef.return_type.clone())?,
        };

        // 跳过 self(仅当存在 receiver)
        for (i, param) in fndef.params.iter().enumerate() {
            if !fndef.is_static && fndef.self_kind != SelfKind::Null && i == 0 {
                continue;
            }
            let mut param_type = {
                let temp_param = param.lock().unwrap();
                temp_param.type_.clone()
            };

            param_type = self.reduction_type(param_type)?;

            type_fn.param_types.push(param_type.clone());

            {
                let mut temp_param = param.lock().unwrap();
                temp_param.type_ = param_type.clone();
            }
        }

        Ok(Type::new(TypeKind::Fn(Box::new(type_fn))))
    }

    fn check_typedef_impl(&mut self, impl_interface: &Type, typedef_ident: String, typedef_stmt: &TypedefStmt) -> Result<(), String> {
        // 获取接口中定义的所有方法
        if let TypeKind::Interface(elements) = &impl_interface.kind {
            // 检查接口中的每个方法是否被实现
            for expect_type in elements {
                if expect_type.status != ReductionStatus::Done {
                    return Err(format!("type '{}' not done", expect_type.ident));
                }

                if let TypeKind::Fn(interface_fn_type) = &expect_type.kind {
                    // 构造实现方法的标识符
                    let fn_ident = format_impl_ident(typedef_ident.clone(), interface_fn_type.name.clone());

                    // 从 typedef_stmt 的 method_table 中查找对应的方法实现
                    let ast_fndef_option = typedef_stmt.method_table.get(&fn_ident);

                    if ast_fndef_option.is_none() {
                        return Err(format!(
                            "type '{}' not impl fn '{}' for interface '{}'",
                            typedef_ident, interface_fn_type.name, impl_interface.ident
                        ));
                    }

                    let ast_fndef = {
                        let temp = ast_fndef_option.unwrap().lock().unwrap();
                        temp.clone()
                    };

                    // 获取实现方法的类型
                    let actual_type = self.infer_impl_fn_decl(&ast_fndef).map_err(|e| e.message)?;

                    // 比较接口方法类型和实现方法类型是否匹配
                    // 注意：实现方法的第一个参数是 self，而接口方法没有
                    if !self.type_compare(expect_type, &actual_type) {
                        return Err(format!(
                            "the fn '{}' of type '{}' mismatch interface '{}'",
                            interface_fn_type.name, typedef_ident, impl_interface.ident
                        ));
                    }
                } else {
                    return Err(format!("interface element must be function type"));
                }
            }
        }

        Ok(())
    }

    fn interface_has_builtin_deny(&self, interface_type: &Type) -> bool {
        interface_type.ident == NONVOID_IDENT
    }

    fn interface_alloc_types_contains(&self, interface_type: &Type, src: &Type) -> bool {
        let kind = match &src.kind {
            TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => value_type.kind.clone(),
            _ => src.kind.clone(),
        };

        match interface_type.ident.as_str() {
            NUMERIC_IDENT => Type::is_number(&kind),
            COMPARABLE_IDENT => Type::is_number(&kind) || kind == TypeKind::String,
            ADDABLE_IDENT => Type::is_number(&kind) || kind == TypeKind::String,
            EQUATABLE_IDENT => Type::is_number(&kind) || kind == TypeKind::Bool || kind == TypeKind::String || kind == TypeKind::Anyptr,
            _ => false,
        }
    }

    fn interface_deny_type(&mut self, interface_type: &Type, src: &Type, visited: &mut HashSet<String>) -> Result<bool, String> {
        if !matches!(interface_type.kind, TypeKind::Interface(..)) {
            return Ok(false);
        }

        let mut target = src.clone();
        if matches!(target.kind, TypeKind::Ref(_) | TypeKind::Ptr(_)) {
            target = match &target.kind {
                TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => *value_type.clone(),
                _ => target,
            };
        }
        target = self.reduction_type(target).map_err(|e| e.message)?;

        if interface_type.ident == NONVOID_IDENT && target.kind == TypeKind::Void {
            return Ok(true);
        }

        if interface_type.ident.is_empty() {
            return Ok(false);
        }
        if visited.contains(&interface_type.ident) {
            return Ok(false);
        }
        visited.insert(interface_type.ident.clone());

        let Some(symbol) = self.symbol_table.find_global_symbol(&interface_type.ident) else {
            return Ok(false);
        };
        let SymbolKind::Type(typedef_mutex) = symbol.kind.clone() else {
            return Ok(false);
        };
        let typedef_stmt = typedef_mutex.lock().unwrap();
        for impl_interface in &typedef_stmt.impl_interfaces {
            let reduced = self.reduction_type(impl_interface.clone()).map_err(|e| e.message)?;
            if self.interface_deny_type(&reduced, &target, visited)? {
                return Ok(true);
            }
        }

        Ok(false)
    }

    fn generics_constrains_check(&mut self, param: &mut GenericsParam, src: &Type) -> Result<(), String> {
        for constraint in &mut param.constraints {
            *constraint = self.reduction_type(constraint.clone()).map_err(|e| e.message)?;
            if !matches!(constraint.kind, TypeKind::Interface(..)) {
                return Err("generic constraints only support interface '&' constraints".to_string());
            }
        }

        if param.constraints.is_empty() {
            return Ok(());
        }

        for expect_interface_type in &param.constraints {
            let mut temp_target_type = src.clone();
            if matches!(src.kind, TypeKind::Ref(_) | TypeKind::Ptr(_)) {
                temp_target_type = match &src.kind {
                    TypeKind::Ref(value_type) | TypeKind::Ptr(value_type) => *value_type.clone(),
                    _ => temp_target_type,
                };
            }

            if self.interface_deny_type(expect_interface_type, &temp_target_type, &mut HashSet::new())? {
                let interface_name = if expect_interface_type.ident.is_empty() {
                    expect_interface_type.to_string()
                } else {
                    expect_interface_type.ident.clone()
                };
                return Err(format!("type '{}' denied by '{}' interface", temp_target_type, interface_name));
            }

            // 空接口 + deny-only：未命中 deny 时直接通过
            if self.interface_has_builtin_deny(expect_interface_type) {
                continue;
            }

            if self.interface_alloc_types_contains(expect_interface_type, &temp_target_type) {
                continue;
            }

            let TypeKind::Interface(elements) = &expect_interface_type.kind else {
                return Err("generic constraints only support interface '&' constraints".to_string());
            };

            // get symbol from symbol table
            let symbol = match self.symbol_table.find_global_symbol(&temp_target_type.ident) {
                Some(s) => s,
                None => {
                    // builtin type: 如果接口有方法要求，则不通过
                    if !elements.is_empty() {
                        return Err(format!(
                            "type '{}' not impl '{}' interface",
                            temp_target_type.ident, expect_interface_type.ident
                        ));
                    }
                    continue;
                }
            };

            if let SymbolKind::Type(typedef_mutex) = symbol.kind.clone() {
                let typedef = typedef_mutex.lock().unwrap();
                let found = self.check_impl_interface_contains(&typedef, expect_interface_type);
                if !found {
                    return Err(format!(
                        "type '{}' not impl '{}' interface",
                        temp_target_type.ident, expect_interface_type.ident
                    ));
                }

                self.check_typedef_impl(expect_interface_type, temp_target_type.ident.clone(), &typedef)?;
            } else {
                unreachable!();
            }
        }

        Ok(())
    }

    fn check_impl_interface_contains(&mut self, typedef_stmt: &TypedefStmt, find_target_interface: &Type) -> bool {
        if typedef_stmt.impl_interfaces.len() == 0 {
            return false;
        }

        for impl_interface in &typedef_stmt.impl_interfaces {
            if impl_interface.ident == find_target_interface.ident {
                return true;
            }

            // find impl interface from symbol table
            let symbol = match self.symbol_table.find_global_symbol(&impl_interface.ident) {
                Some(s) => s,
                None => continue,
            };

            if let SymbolKind::Type(def_type_mutex) = symbol.kind.clone() {
                let t = def_type_mutex.lock().unwrap();
                if self.check_impl_interface_contains(&t, find_target_interface) {
                    return true;
                }
            }
        }

        return false;
    }

    pub fn pre_infer(&mut self) -> Vec<AnalyzerError> {
        // 遍历 module 下的所有的 fndef, 包含 global fn 和 local fn
        let global_fndefs = self.module.global_fndefs.clone();
        for fndef_mutex in global_fndefs {
            let (is_generics, local_children) = {
                let fndef = fndef_mutex.lock().unwrap();
                (fndef.is_generics, fndef.local_children.clone())
            };

            // 不是泛型函数
            if !is_generics {
                if let Err(e) = self.infer_fn_decl(fndef_mutex.clone(), Type::default()) {
                    self.errors_push(e.start, e.end, e.message);
                }

                for child_fndef_mutex in local_children {
                    if let Err(e) = self.infer_fn_decl(child_fndef_mutex, Type::default()) {
                        self.errors_push(e.start, e.end, e.message);
                    }
                }

                continue;
            }
        }

        return self.errors.clone();
    }

    pub fn errors_push(&mut self, start: usize, end: usize, message: String) {
        // 判断 current fn 是否属于当前 module
        let current_fn = self.current_fn_mutex.lock().unwrap();
        if current_fn.module_index > 0 && current_fn.module_index != self.module.index {
            return;
        }

        errors_push(self.module, AnalyzerError { start, end, message });
    }

    pub fn infer(&mut self) -> Vec<AnalyzerError> {
        for fndef_mutex in self.module.global_fndefs.clone() {
            let fndef = fndef_mutex.lock().unwrap();
            // generics fn 不需要进行类型推倒，因为其 param 是不确定的，只需要被调用时由调用方进行 推导
            if fndef.is_generics {
                continue;
            }

            // 经过了 pre_inf fn 的类型必须是确定的，如果不确定则可能是 pre infer 推导类型异常出了什么错误
            if !matches!(fndef.type_.kind, TypeKind::Fn(_)) {
                continue;
            }
            self.worklist.push(fndef_mutex.clone());
        }

        // handle infer worklist, temp data to self
        while let Some(fndef_mutex) = self.worklist.pop() {
            // 先获取需要的数据
            let generics_args_table = {
                let fndef = fndef_mutex.lock().unwrap();
                fndef.generics_args_table.clone()
            };

            if let Some(ref table) = generics_args_table {
                self.generics_args_stack.push(table.clone());
            }

            // clone 只是增加了 arc 的引用计数，内部还是共用的一个锁，所以需要注意死锁问题
            self.infer_fndef(fndef_mutex.clone());

            let fndef = fndef_mutex.lock().unwrap();
            // handle child, 共用 generics arg table
            for child_fndef_mutex in fndef.local_children.clone() {
                self.infer_fndef(child_fndef_mutex);
            }

            if generics_args_table.is_some() {
                self.generics_args_stack.pop();
            }
        }

        return self.errors.clone();
    }
}
