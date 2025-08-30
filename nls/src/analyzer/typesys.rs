use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
};

use std::collections::hash_map::DefaultHasher;
use std::collections::HashSet;
use std::hash::{Hash, Hasher};

use log::debug;

use super::{
    common::{AnalyzerError, AstCall, AstNode, Expr, Stmt, Type, TypeFn, TypedefStmt, VarDeclExpr},
    symbol::{NodeId, SymbolTable},
};
use crate::{
    analyzer::{common::*, symbol::SymbolKind},
    project::Module,
    utils::{errors_push, format_generics_ident, format_impl_ident},
};

#[derive(Debug, Clone)]
pub struct GenericSpecialFnClone {
    // default 是 none, clone 过程中, 当 global fn clone 完成后，将 clone 完成的 global fn 赋值给 global_parent
    global_parent: Option<Arc<Mutex<AstFnDef>>>,
}

impl GenericSpecialFnClone {
    pub fn deep_clone(&mut self, fn_mutex: &Arc<Mutex<AstFnDef>>) -> Arc<Mutex<AstFnDef>> {
        let fn_def = fn_mutex.lock().unwrap();
        let mut fn_def_clone = fn_def.clone();
        fn_def_clone.generics_hash_table = None;
        fn_def_clone.generics_special_done = false;

        // type 中不包含 arc, 所以可以直接进行 clone
        fn_def_clone.type_ = fn_def.type_.clone();
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
                    value: Some(Box::new(self.clone_expr(&property.value.as_ref().unwrap()))),
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
            AstNode::As(type_, src) => AstNode::As(type_.clone(), Box::new(self.clone_expr(src))),
            AstNode::Is(type_, src) => AstNode::Is(type_.clone(), Box::new(self.clone_expr(src))),
            AstNode::MatchIs(type_) => AstNode::MatchIs(type_.clone()),
            AstNode::Catch(try_expr, catch_err, catch_body) => AstNode::Catch(
                Box::new(self.clone_expr(try_expr)),
                Arc::new(Mutex::new(catch_err.lock().unwrap().clone())),
                self.clone_body(catch_body),
            ),
            AstNode::Match(subject, cases) => AstNode::Match(subject.as_ref().map(|s| Box::new(self.clone_expr(s))), self.clone_match_cases(cases)),

            AstNode::MacroSizeof(target_type) => AstNode::MacroSizeof(target_type.clone()),
            AstNode::MacroDefault(target_type) => AstNode::MacroDefault(target_type.clone()),
            AstNode::MacroUla(src) => AstNode::MacroUla(Box::new(self.clone_expr(src))),
            AstNode::MacroReflectHash(type_) => AstNode::MacroReflectHash(type_.clone()),
            AstNode::MacroTypeEq(left, right) => AstNode::MacroTypeEq(left.clone(), right.clone()),

            AstNode::ArrayAccess(type_, left, index) => AstNode::ArrayAccess(type_.clone(), Box::new(self.clone_expr(left)), Box::new(self.clone_expr(index))),

            AstNode::SelectExpr(left, key) => AstNode::SelectExpr(Box::new(self.clone_expr(left)), key.clone()),
            _ => expr.node.clone(),
        };

        Expr {
            start: expr.start,
            end: expr.end,
            type_: expr.type_.clone(),
            target_type: expr.target_type.clone(),
            node,
            err: expr.err,
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
            TypeKind::Struct(ident, align, properties) => {
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
        }

        if args.len() > 0 {
            result.args = args;
        }

        result.status = ReductionStatus::Done;
        result.kind = Type::cross_kind_trans(&result.kind);

        // recycle_check
        let mut visited = HashSet::new();
        let found = self.type_recycle_check(&t, &mut visited);
        if found.is_some() {
           return Err(AnalyzerError{
                start: t.start,
                end: t.end,
                message: format!("recycle use type '{}'", found.unwrap()),
            })
        }

        return Ok(result);
    }

    fn combination_interface(&mut self, typedef_stmt: &mut TypedefStmt) -> Result<(), AnalyzerError> {
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
            *impl_interface = match self.reduction_type(impl_interface.clone()) {
                Ok(r) => r,
                Err(e) => continue,
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

    fn reduction_type_ident(&mut self, mut t: Type) -> Result<Type, AnalyzerError> {
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

        let mut typedef = typedef_mutex.lock().unwrap();
        let typedef_start = typedef.symbol_start;
        let typedef_end = typedef.symbol_end;

        // 处理泛型参数
        if typedef.params.len() > 0 {
            // 检查是否提供了泛型参数
            if t.args.is_empty() {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: format!("typedef '{}' need params", t.ident),
                });
            }
            // 检查参数数量是否匹配
            if t.args.len() != typedef.params.len() {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: format!("typedef '{}' params mismatch", t.ident),
                });
            }

            // 创建参数表并压入栈
            let mut args_table = HashMap::new();
            let mut impl_args = Vec::new();
            for (i, undo_arg) in t.args.iter_mut().enumerate() {
                // 先进行类型归约
                let arg = self.reduction_type(undo_arg.clone())?;

                let param = &mut typedef.params[i];

                // 检查泛型约束
                if let Err(e) = self.generics_constrains_check(param, &arg) {
                    return Err(AnalyzerError {
                        start,
                        end,
                        message: format!("generics constraint check failed: {}", e),
                    });
                }

                impl_args.push(arg.clone());
                args_table.insert(param.ident.clone(), arg.clone());
            }

            // 压入参数表用于后续处理
            self.generics_args_stack.push(args_table);

            // 处理接口实现
            if typedef.impl_interfaces.len() > 0 {
                if typedef.is_interface {
                    debug_assert!(typedef.type_expr.status == ReductionStatus::Done);
                    debug_assert!(matches!(typedef.type_expr.kind, TypeKind::Interface(..)));
                    self.combination_interface(&mut typedef)?
                } else {
                    for impl_interface in &mut typedef.impl_interfaces {
                        *impl_interface = self.reduction_type(impl_interface.clone())?;
                    }

                    for impl_interface in &typedef.impl_interfaces {
                        self.check_typedef_impl(impl_interface, t.ident.clone(), &typedef)
                            .map_err(|e| AnalyzerError { start, end, message: e })?;
                    }
                }
            }

            // 处理右值表达式
            let mut right_type = typedef.type_expr.clone();

            right_type = self.reduction_type(right_type)?;

            // 弹出参数表
            self.generics_args_stack.pop();

            t.args = impl_args;
            t.kind = right_type.kind;
            t.status = right_type.status;
            return Ok(t);
        } else {
            // params == 0
            if !t.args.is_empty() {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: format!("typedef '{}' args mismatch", t.ident),
                });
            }
        }

        // 检查右值是否已完成归约
        if typedef.type_expr.status == ReductionStatus::Done {
            t.kind = typedef.type_expr.kind.clone();
            t.status = typedef.type_expr.status;
            return Ok(t);
        }

        // 处理循环引用
        if typedef.type_expr.status == ReductionStatus::Doing2 {
            return Ok(t);
        }

        // 标记正在处理,避免循环引用
        if (typedef.type_expr.status == ReductionStatus::Doing) {
            typedef.type_expr.status = ReductionStatus::Doing2;
        } else {
            typedef.type_expr.status = ReductionStatus::Doing;
        }

        // 处理接口
        if typedef.is_interface {
            typedef.type_expr.ident_kind = TypeIdentKind::Interface;
            typedef.type_expr.ident = t.ident.clone();
        }
        let type_expr = typedef.type_expr.clone();

        // 在递归调用前释放锁，避免死锁
        drop(typedef);

        // 归约类型表达式递归处理, 避免 typedef_mutex 锁定
        let type_expr = match self.reduction_type(type_expr) {
            Ok(type_expr) => type_expr,
            Err(e) => {
                // change typedef status to undo
                let mut typedef = typedef_mutex.lock().unwrap();
                if typedef.type_expr.status == ReductionStatus::Doing {
                    typedef.type_expr.status = ReductionStatus::Undo;
                }

                return Err(e);
            }
        };

        let mut typedef = typedef_mutex.lock().unwrap();
        typedef.type_expr = type_expr;

        // 处理接口实现
        if typedef.impl_interfaces.len() > 0 {
            if typedef.is_interface {
                debug_assert!(typedef.type_expr.status == ReductionStatus::Done);
                debug_assert!(matches!(typedef.type_expr.kind, TypeKind::Interface(..)));
                self.combination_interface(&mut typedef)?
            } else {
                for impl_interface in &mut typedef.impl_interfaces {
                    *impl_interface = self.reduction_type(impl_interface.clone())?;
                }

                for impl_interface in &typedef.impl_interfaces {
                    self.check_typedef_impl(impl_interface, t.ident.clone(), &typedef)
                        .map_err(|e| AnalyzerError { start, end, message: e })?;
                }
            }
        }

        t.kind = typedef.type_expr.kind.clone();
        t.status = typedef.type_expr.status;
        Ok(t)
    }

    fn reduction_complex_type(&mut self, t: Type) -> Result<Type, AnalyzerError> {
        let mut result = t.clone();

        let kind_str = result.kind.to_string();

        match &mut result.kind {
            // 处理指针类型
            TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => {
                *value_type = Box::new(self.reduction_type(*value_type.clone())?);
            }

            // 处理数组类型
            TypeKind::Arr(_, _, element_type) => {
                *element_type = Box::new(self.reduction_type(*element_type.clone())?);
            }

            // 处理通道类型
            TypeKind::Chan(element_type) => {
                *element_type = Box::new(self.reduction_type(*element_type.clone())?);

                result.ident_kind = TypeIdentKind::Builtin;
                result.ident = kind_str;
                result.args.push(*element_type.clone());
            }

            // 处理向量类型
            TypeKind::Vec(element_type) => {
                *element_type = Box::new(self.reduction_type(*element_type.clone())?);

                result.ident_kind = TypeIdentKind::Builtin;
                result.ident = kind_str;
                result.args = vec![*element_type.clone()];
            }

            // 处理映射类型
            TypeKind::Map(key_type, value_type) => {
                *key_type = Box::new(self.reduction_type(*key_type.clone())?);
                *value_type = Box::new(self.reduction_type(*value_type.clone())?);

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
                *element_type = Box::new(self.reduction_type(*element_type.clone())?);

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
            TypeKind::Tuple(elements, align) => {
                if elements.is_empty() {
                    return Err(AnalyzerError {
                        start: t.start,
                        end: t.end,
                        message: "tuple element empty".to_string(),
                    });
                }

                for element_type in elements.iter_mut() {
                    *element_type = self.reduction_type(element_type.clone())?;
                }
            }
            TypeKind::Fn(type_fn) => {
                type_fn.return_type = self.reduction_type(type_fn.return_type.clone())?;

                for formal_type in type_fn.param_types.iter_mut() {
                    *formal_type = self.reduction_type(formal_type.clone())?;
                }
            }

            // 处理结构体类型
            TypeKind::Struct(_ident, _align, properties) => {
                for property in properties.iter_mut() {
                    if !property.type_.kind.is_unknown() {
                        property.type_ = self.reduction_type(property.type_.clone())?;
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

    pub fn type_param_special(&mut self, t: Type, arg_table: HashMap<String, Type>) -> Type {
        debug_assert!(t.kind == TypeKind::Ident);
        debug_assert!(t.ident_kind == TypeIdentKind::GenericsParam);

        let arg_type = arg_table.get(&t.ident).unwrap();
        return self.reduction_type(arg_type.clone()).unwrap();
    }

    pub fn reduction_type(&mut self, mut t: Type) -> Result<Type, AnalyzerError> {
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
            t = self.reduction_type_ident(t)?;
            // 如果原来存在 t.args 则其经过了 reduction
            return self.finalize_type(t.clone(), ident, ident_kind, t.args);
        }

        if t.kind == TypeKind::Ident && t.ident_kind == TypeIdentKind::GenericsParam {
            if self.generics_args_stack.is_empty() {
                return self.finalize_type(t, ident, ident_kind, args);
            }

            let arg_table = self.generics_args_stack.last().unwrap();
            let result = self.type_param_special(t, arg_table.clone());

            return self.finalize_type(result.clone(), result.ident.clone(), result.ident_kind, result.args);
        }

        match &mut t.kind {
            TypeKind::Union(any, _, elements) => {
                if *any {
                    return self.finalize_type(t, ident, ident_kind, args);
                }

                for element in elements {
                    *element = self.reduction_type(element.clone())?;
                }

                return self.finalize_type(t, ident, ident_kind, args);
            }
            TypeKind::Interface(elements) => {
                for element in elements {
                    *element = self.reduction_type(element.clone())?;
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
                    let result = self.reduction_complex_type(t)?;
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

    pub fn infer_as_expr(&mut self, expr: &mut Box<Expr>) -> Result<Type, AnalyzerError> {
        // 先还原目标类型
        let AstNode::As(target_type, src) = &mut expr.node else { unreachable!() };
        *target_type = self.reduction_type(target_type.clone())?;

        // 推导源表达式类型, 如果存在错误则停止后续比较, 直接返回错误
        let src_type = self.infer_expr(src, Type::default())?;
        if src_type.kind.is_unknown() {
            return Err(AnalyzerError {
                start: 0,
                end: 0,
                message: "unknown as source type".to_string(),
            });
        }

        src.type_ = src_type.clone();

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
                    start: expr.start,
                    end: expr.end,
                    message: "union to union type is not supported".to_string(),
                });
            }

            if !self.union_type_contains(&(*any, elements.clone()), &target_type) {
                return Err(AnalyzerError {
                    start: expr.start,
                    end: expr.end,
                    message: format!("type {} not contains in union type", target_type),
                });
            }

            return Ok(target_type.clone());
        }

        // 处理接口类型转换
        if let TypeKind::Interface(_) = &src_type.kind {
            // interface_type = src_type
            let temp_target_type = match &target_type.kind {
                TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => *value_type.clone(),
                _ => target_type.clone(),
            };

            if temp_target_type.ident.is_empty() || temp_target_type.ident_kind != TypeIdentKind::Def {
                return Err(AnalyzerError {
                    start: expr.start,
                    end: expr.end,
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
                        start: expr.start,
                        end: expr.end,
                        message: format!("type '{}' not impl '{}' interface", temp_target_type.ident, src_type),
                    });
                }

                self.check_typedef_impl(&src_type, temp_target_type.ident.clone(), &typedef)
                    .map_err(|e| AnalyzerError {
                        start: expr.start,
                        end: expr.end,
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
                self.literal_as_check(literal_kind, literal_value, target_type.kind.clone())
                    .map_err(|e| AnalyzerError {
                        start: expr.start,
                        end: expr.end,
                        message: e,
                    })?;
            }

            return Ok(target_type.clone());
        }

        // 处理 typedef ident 类型转换
        if target_type.ident_kind == TypeIdentKind::Def {
            let ident = target_type.ident.clone();
            let ident_kind = target_type.ident_kind.clone();
            let args = target_type.args.clone();

            src.type_.ident = target_type.ident.clone();
            src.type_.ident_kind = target_type.ident_kind.clone();
            src.type_.args = target_type.args.clone();

            // compare check
            if self.type_compare(&src.type_, target_type) {
                src.type_.ident = ident;
                src.type_.ident_kind = ident_kind;
                src.type_.args = args;
                return Ok(target_type.clone());
            }

            return Err(AnalyzerError {
                start: expr.start,
                end: expr.end,
                message: format!("cannot casting to '{}'", target_type),
            });
        }

        if src.type_.ident_kind == TypeIdentKind::Def && target_type.ident_kind == TypeIdentKind::Builtin {
            let mut src_temp_type = src.type_.clone();
            src_temp_type.ident = target_type.ident.clone();
            src_temp_type.ident_kind = target_type.ident_kind.clone();

            if self.type_compare(&target_type, &src_temp_type) {
                src.type_ = src_temp_type.clone();
                return Ok(target_type.clone());
            }
        }

        // 检查目标类型是否可以进行类型转换
        return Err(AnalyzerError {
            start: expr.start,
            end: expr.end,
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

        // 用于跟踪联合类型的匹配情况
        let mut union_types = HashMap::new();
        let mut has_default = false;

        // 遍历所有 case
        for case in cases {
            if case.is_default {
                has_default = true;
            } else {
                // 处理每个条件表达式
                for cond_expr in case.cond_list.iter_mut() {
                    // 对于联合类型,只能使用 is 匹配
                    if matches!(subject_type.kind, TypeKind::Union(..)) {
                        if !matches!(cond_expr.node, AstNode::MatchIs(..)) {
                            return Err(AnalyzerError {
                                start: cond_expr.start,
                                end: cond_expr.end,
                                message: "match 'union type' only support 'is' assert".to_string(),
                            });
                        }
                    }

                    // 处理 is 类型匹配
                    if let AstNode::MatchIs(_target_type) = cond_expr.node.clone() {
                        if !matches!(subject_type.kind, TypeKind::Union(..) | TypeKind::Interface(..)) {
                            return Err(AnalyzerError {
                                start: cond_expr.start,
                                end: cond_expr.end,
                                message: format!("{} cannot use 'is' operator", subject_type),
                            });
                        }

                        let cond_type = self.infer_right_expr(cond_expr, Type::default())?;
                        debug_assert!(matches!(cond_type.kind, TypeKind::Bool));

                        let AstNode::MatchIs(target_type) = &cond_expr.node else { unreachable!() };

                        // 记录已匹配的类型, 最终可以判断 match 是否匹配了所有分支
                        union_types.insert(target_type.hash(), true);
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
                        if !union_types.contains_key(&element_type.hash()) {
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

    pub fn infer_unary(&mut self, op: ExprOp, operand: &mut Box<Expr>, target_type: Type) -> Result<Type, AnalyzerError> {
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

            return Ok(Type::rawptr_of(operand_type));
        }

        // 处理不安全取地址运算符 @unsafe_la
        if op == ExprOp::UnsafeLa {
            // 检查是否是字面量或函数调用
            if matches!(operand.node, AstNode::Literal(..) | AstNode::Call(..)) {
                return Err(AnalyzerError {
                    start: operand.start,
                    end: operand.end,
                    message: "cannot safe load address of an literal or call".to_string(),
                });
            }

            // 检查是否是联合类型
            if matches!(operand_type.kind, TypeKind::Union(..)) {
                return Err(AnalyzerError {
                    start: operand.start,
                    end: operand.end,
                    message: "cannot safe load address of an union type".to_string(),
                });
            }

            return Ok(Type::ptr_of(operand_type));
        }

        // 处理解引用运算符 *
        if op == ExprOp::Ia {
            // 检查是否是指针类型
            match operand_type.kind {
                TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => {
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
        // debug!(
        //     "infer_ident {} symbol_ident: {} {} {} {}",
        //     ident, &symbol.ident, &symbol.is_local, &symbol.defined_in, *symbol_id
        // );

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
            SymbolKind::Fn(fndef) => {
                return self.infer_fn_decl(fndef.clone(), Type::default());
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

    pub fn infer_vec_new(&mut self, expr: &mut Box<Expr>, infer_target_type: Type) -> Result<Type, AnalyzerError> {
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

    pub fn infer_map_new(&mut self, elements: &mut Vec<MapElement>, infer_target_type: Type, start: usize, end: usize) -> Result<Type, AnalyzerError> {
        // 创建一个新的 Map 类型，初始化 key 和 value 类型为未知
        let mut key_type = Type::default(); // TYPE_UNKNOWN
        let mut value_type = Type::default(); // TYPE_UNKNOWN

        // 如果有目标类型且是Map类型，使用目标类型的key和value类型
        if let TypeKind::Map(target_key_type, target_value_type) = &infer_target_type.kind {
            key_type = *target_key_type.clone();
            value_type = *target_value_type.clone();
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
            TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => {
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

    pub fn infer_select_expr(&mut self, expr: &mut Box<Expr>) -> Result<Type, AnalyzerError> {
        let AstNode::SelectExpr(left, key) = &mut expr.node else { unreachable!() };

        // 先推导左侧表达式的类型
        let left_type = self.infer_right_expr(left, Type::default())?;

        // 处理自动解引用 - 如果是指针类型且指向结构体，则获取结构体类型
        let mut deref_type = match &left_type.kind {
            TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => {
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
                err: false,
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
                err: false,
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

    pub fn infer_expr(&mut self, expr: &mut Box<Expr>, infer_target_type: Type) -> Result<Type, AnalyzerError> {
        if expr.type_.kind.is_exist() {
            return Ok(expr.type_.clone());
        }

        return match &mut expr.node {
            AstNode::As(_, _) => self.infer_as_expr(expr),
            AstNode::Catch(try_expr, catch_err_mutex, catch_body) => self.infer_catch(try_expr, catch_err_mutex, catch_body),
            AstNode::Match(subject, cases) => self.infer_match(subject, cases, infer_target_type, expr.start, expr.end),
            AstNode::MatchIs(target_type) => {
                *target_type = self.reduction_type(target_type.clone())?;
                return Ok(Type::new(TypeKind::Bool));
            }
            AstNode::Is(target_type, src) => {
                let src_type = self.infer_right_expr(src, Type::default())?;

                *target_type = self.reduction_type(target_type.clone())?;
                if !matches!(src_type.kind, TypeKind::Union(..) | TypeKind::Interface(..)) {
                    return Err(AnalyzerError {
                        start: expr.start,
                        end: expr.end,
                        message: format!("{} cannot use 'is' operator", src_type),
                    });
                }

                return Ok(Type::new(TypeKind::Bool));
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
            AstNode::MacroUla(src) => {
                let src_type = self.infer_right_expr(src, Type::default())?;
                return Ok(Type::ptr_of(src_type));
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

                return self.reduction_type(Type::ptr_of(type_.clone()));
            }
            AstNode::Binary(op, left, right) => self.infer_binary(op.clone(), left, right, infer_target_type),
            AstNode::Unary(op, operand) => self.infer_unary(op.clone(), operand, infer_target_type),
            AstNode::Ident(ident, symbol_id) => self.infer_ident(ident, symbol_id, expr.start, expr.end),
            AstNode::VecNew(..) => self.infer_vec_new(expr, infer_target_type),
            AstNode::VecRepeatNew(..) | AstNode::ArrRepeatNew(..) => self.infer_vec_repeat_new(expr, infer_target_type),
            AstNode::VecSlice(left, start, end) => {
                let left_type = self.infer_right_expr(left, Type::default())?;
                self.infer_right_expr(start, Type::integer_t_new())?;
                self.infer_right_expr(end, Type::integer_t_new())?;

                return Ok(left_type.clone());
            }
            AstNode::EmptyCurlyNew => {
                if infer_target_type.kind.is_unknown() {
                    return Ok(infer_target_type);
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
            AstNode::MapNew(elements) => self.infer_map_new(elements, infer_target_type, expr.start, expr.end),
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
            TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => *value_type.clone(),
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
            node: AstNode::As(interface_type.clone(), expr.clone()),
            err: false,
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
                    Type::new(TypeKind::Unknown)
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
            expr.node = AstNode::As(target_type.clone(), expr.clone());
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
        let mut hash_str = "fn".to_string();

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

    fn impl_call_rewrite(&mut self, call: &mut AstCall, start: usize, end: usize) -> Result<bool, AnalyzerError> {
        // 获取select表达式
        let AstNode::SelectExpr(select_left, key) = &mut call.left.node.clone() else {
            unreachable!()
        };

        // 获取left的类型(已经在之前推导过)
        let select_left_type = self.infer_right_expr(select_left, Type::default())?;

        // 解构类型判断
        let select_left_type = if matches!(select_left_type.kind, TypeKind::Ptr(_) | TypeKind::Rawptr(_)) {
            match &select_left_type.kind {
                TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => *value_type.clone(),
                _ => unreachable!(),
            }
        } else {
            select_left_type.clone()
        };

        // 如果是 struct 类型且 key 是其属性,不需要重写
        if let TypeKind::Struct(_, _, properties) = &select_left_type.kind {
            if properties.iter().any(|p| p.name == *key) {
                return Ok(false);
            }
        }

        // 比如 int? 转换为 union 时就不存在 ident
        if select_left_type.ident.is_empty() {
            return Ok(false);
        }

        let mut impl_ident = select_left_type.ident.clone();
        let mut impl_args = select_left_type.args.clone();

        // string.len() -> string_len
        // person_t.set_age() -> person_t_set_age()
        // register_global_symbol 中进行类 impl_foramt
        let mut impl_symbol_name = format_impl_ident(impl_ident.clone(), key.clone());

        let (final_symbol_name, symbol_id) = match self.find_impl_call_ident(impl_symbol_name.clone(), impl_args.clone(), &select_left_type) {
            Ok(r) => r,
            Err(_e) => {
                // idetn to default kind(need args)
                let mut builtin_type = select_left_type.clone();
                builtin_type.ident = "".to_string();
                builtin_type.ident_kind = TypeIdentKind::Unknown;
                builtin_type.args = Vec::new();
                builtin_type.status = ReductionStatus::Undo;
                builtin_type = self.reduction_type(builtin_type)?;

                // builtin 测试
                if builtin_type.ident.len() > 0 && builtin_type.ident_kind == TypeIdentKind::Builtin {
                    impl_ident = builtin_type.ident.clone();
                    impl_args = builtin_type.args.clone();
                    impl_symbol_name = format_impl_ident(impl_ident.clone(), key.clone());

                    // 直接返回第二次查找的结果，成功时返回结果，失败时返回错误
                    match self.find_impl_call_ident(impl_symbol_name.clone(), impl_args.clone(), &select_left_type) {
                        Ok(result) => {
                            // change self arg 类型
                            match &mut select_left.type_.kind {
                                TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => {
                                    *value_type = Box::new(builtin_type);
                                }
                                _ => {
                                    select_left.type_ = builtin_type;
                                }
                            }

                            result
                        }
                        Err(_) => {
                            return Err(AnalyzerError {
                                start,
                                end,
                                message: format!("type '{}' no impl fn '{}'", select_left_type, key),
                            });
                        }
                    }
                } else {
                    return Err(AnalyzerError {
                        start,
                        end,
                        message: format!("type '{}' no impl fn '{}'", select_left_type, key),
                    });
                }
            }
        };

        // 重写 select call 为 ident call
        call.left = Box::new(Expr::ident(call.left.start, call.left.end, final_symbol_name, symbol_id));

        // 构建新的参数列表
        let mut new_args = Vec::new();

        // 添加self参数
        let mut self_arg = select_left.clone(); // {'a':1}.del('a') -> map_del({'a':1}, 'a')
        if self_arg.type_.is_stack_impl() {
            // 生成 la expr &sself_arg
            self_arg.node = AstNode::Unary(ExprOp::La, self_arg.clone());
            self_arg.type_ = Type::ptr_of(self_arg.type_.clone());
        }
        new_args.push(self_arg);

        new_args.extend(call.args.iter().cloned());

        call.args = new_args;

        // 设置泛型参数
        call.generics_args = impl_args;

        Ok(true)
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

        // 如果存在impl_type,必须提供泛型参数
        if temp_fndef.impl_type.kind.is_exist() {
            debug_assert!(!generics_args.is_empty());
        }

        // call generics args 为 null 说明 caller 没有传递泛型 args, 此时需要根据参数进行泛型推导
        if generics_args.is_empty() {
            // 保存当前的类型参数栈
            let stash_stack = self.generics_args_stack.clone();
            self.generics_args_stack.clear();

            // 遍历所有参数进行类型推导
            let args_len = args.len();
            for (i, arg) in args.iter_mut().enumerate() {
                let is_spread = spread && (i == args_len - 1);

                // 获取实参类型 (arg 在 infer right expr 中会被修改 type_ 和 target_type 字段)
                let arg_type = self.infer_right_expr(arg, Type::default()).map_err(|e| e.message)?;

                let formal_type = self.select_generics_fn_param(temp_fndef.clone(), i, is_spread);
                if formal_type.err {
                    return Err(format!("too many arguments"));
                }

                // 对形参类型进行还原
                let temp_type = self.reduction_type(formal_type.clone()).map_err(|e| e.message)?;

                // 比较类型并填充泛型参数表
                if !self.type_generics(&temp_type, &arg_type, &mut table) {
                    return Err(format!("cannot infer generics type from {} to {}", arg_type, temp_type));
                }
            }

            // 处理返回类型约束
            if !matches!(
                return_target_type.kind,
                TypeKind::Unknown | TypeKind::Void | TypeKind::Union(..) | TypeKind::Null
            ) {
                let temp_type = self.reduction_type(temp_fndef.return_type.clone()).map_err(|e| e.message)?;

                if !self.type_compare(&temp_type, &return_target_type) {
                    return Err(format!("return type infer failed, expect={}, actual={}", return_target_type, temp_type));
                }
            }

            // 检查所有泛型参数是否都已推导
            if let Some(generics_params) = &mut temp_fndef.generics_params {
                for param in generics_params {
                    let arg_type = match table.get(&param.ident) {
                        Some(arg_type) => arg_type,
                        None => return Err(format!("cannot infer generics param '{}'", param.ident)),
                    };

                    self.generics_constrains_check(param, arg_type)?
                }
            }

            // 恢复类型参数栈
            self.generics_args_stack = stash_stack;
        } else {
            // user call 以及提供的泛型参数， 直接使用提供的泛型参数
            if let Some(generics_params) = &temp_fndef.generics_params {
                for (i, arg_type) in generics_args.into_iter().enumerate() {
                    let reduced_type = self.reduction_type(arg_type).map_err(|e| e.message)?;
                    table.insert(generics_params[i].ident.clone(), reduced_type);
                }
            }
        }

        Ok(table)
    }

    fn generics_args_hash(&mut self, generics_params: &Vec<GenericsParam>, args_table: HashMap<String, Type>) -> u64 {
        let mut hash_str = "fn.".to_string();

        // 遍历所有泛型参数
        for param in generics_params {
            // 从args_table中获取对应的类型
            let t = args_table.get(&param.ident).unwrap();

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
            //temp_fndef.symbol_name@args_hash
            format_generics_ident(temp_fndef.symbol_name.clone(), args_hash)
        };

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

        // tpl_fn 对应的 special_fn 已经生成过则不需要重新生成，直接返回即可
        {
            let mut tpl_fn = tpl_fn_mutex.lock().unwrap();

            // 判断 tpl fn 是否已经 special 完成，如果已经完成，则直接返回, 为什么会出现这种情况?
            // 相同参数得到相同的 arg_hash, 当相同 arg_hash 读取 tpl_fn 时，会从符号表中优先读取到已经存在且推导完成的 special_fn, 直接使用即可。
            if tpl_fn.generics_special_done {
                return Ok(tpl_fn_mutex.clone());
            }

            if tpl_fn.generics_hash_table.is_none() {
                tpl_fn.generics_hash_table = Some(HashMap::new());
            }

            let generics_hash_table = tpl_fn.generics_hash_table.as_ref().unwrap();

            // special fn exists
            if let Some(special_fn) = generics_hash_table.get(&args_hash) {
                return Ok(special_fn.clone());
            }
        }

        // lsp 中无论是 否 singleton 都会 clone 一份, 因为 ide 会随时会修改文件从而新增泛型示例，必须保证 tpl fn 是无污染的
        let special_fn_mutex = {
            let result = GenericSpecialFnClone { global_parent: None }.deep_clone(&tpl_fn_mutex);

            let mut tpl_fn = tpl_fn_mutex.lock().unwrap();
            tpl_fn.generics_hash_table.as_mut().unwrap().insert(args_hash, result.clone());

            result
        };

        {
            let mut special_fn = special_fn_mutex.lock().unwrap();
            special_fn.generics_args_hash = Some(args_hash);
            special_fn.generics_args_table = Some(args_table);
            special_fn.generics_special_done = true;
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
            // set type_args_stack
            self.generics_args_stack.push(special_fn.generics_args_table.clone().unwrap());
        }

        self.infer_fn_decl(special_fn_mutex.clone(), Type::default()).map_err(|e| e.message)?;

        self.worklist.push(special_fn_mutex.clone());

        // handle child
        {
            let special_fn = special_fn_mutex.lock().unwrap();
            // dbg!("special fn to work list, will infer body", &special_fn.symbol_name, &special_fn.type_);

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

            // 由于存在简单的函数重载，所以需要进行多次批评找到合适的函数， 如果没有重载， fndef 就是目标函数
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

    pub fn infer_call_left(&mut self, call: &mut AstCall, target_type: Type, start: usize, end: usize) -> Result<TypeKind, AnalyzerError> {
        // --------------------------------------------interface call handle----------------------------------------------------------
        if let AstNode::SelectExpr(select_left, key) = &mut call.left.node {
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

        // --------------------------------------------impl type handle----------------------------------------------------------
        if let AstNode::SelectExpr(_, _) = &mut call.left.node {
            let _is_rewrite = self.impl_call_rewrite(call, start, end)?;
        }

        // --------------------------------------------generics handle----------------------------------------------------------
        if let AstNode::Ident(ident, symbol_id) = &mut call.left.node {
            if *symbol_id == 0 {
                return Err(AnalyzerError {
                    start,
                    end,
                    message: format!("symbol '{}' not found", ident),
                });
            }

            match self.infer_generics_special(
                target_type.clone(),
                *symbol_id,
                (call.args.clone(), call.generics_args.clone(), call.spread.clone()),
            ) {
                Ok(result) => {
                    match result {
                        Some(special_fn) => {
                            let special_fn = special_fn.lock().unwrap();

                            // ident 重写
                            *ident = special_fn.symbol_name.clone();
                            *symbol_id = special_fn.symbol_id; // change call ident actual symbol_id
                        }
                        None => {} // local fn 或者 no generics param 都不是 generics fn, 此时什么都不做
                    }
                }
                Err(e) => {
                    return Err(AnalyzerError { start, end, message: e });
                }
            };
        }

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
        if call.left.err {
            return Ok(Type::error());
        }

        let fn_kind: TypeKind = self.infer_call_left(call, target_type, start, end)?;

        let TypeKind::Fn(type_fn) = fn_kind else { unreachable!() };
        self.infer_call_args(call, *type_fn.clone());
        call.return_type = type_fn.return_type.clone();

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

    pub fn type_compare(&mut self, dst: &Type, src: &Type) -> bool {
        let dst = dst.clone();
        if dst.err || src.err {
            return false;
        }

        debug_assert!(!Type::ident_is_generics_param(&dst));
        debug_assert!(!Type::ident_is_generics_param(&src));

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

        // rawptr<t> 可以赋值为 null 以及 ptr<t>
        if let TypeKind::Rawptr(dst_value_type) = &dst.kind {
            match &src.kind {
                TypeKind::Null => return true,
                TypeKind::Ptr(src_value_type) => {
                    return self.type_compare(dst_value_type, src_value_type);
                }
                _ => {}
            }
        }

        // 即使 reduction 后，也需要通过 ident kind 进行判断, 其中 union_type 不受 type def 限制, TODO 无法区分这是 type_compare 还是 generics param handle!
        if dst.ident_kind == TypeIdentKind::Def && !matches!(dst.kind, TypeKind::Union(..)) {
            debug_assert!(!dst.ident.is_empty());
            if src.ident.is_empty() {
                return false;
            }

            if dst.ident != src.ident {
                return false;
            }

            return true;
        }

        if src.ident_kind == TypeIdentKind::Def && !matches!(src.kind, TypeKind::Union(..)) {
            debug_assert!(!src.ident.is_empty());
            if dst.ident.is_empty() {
                return false;
            }

            if dst.ident != src.ident {
                return false;
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
                if *left_any {
                    return true;
                }
                return self.type_union_compare(&(*left_any, left_types.clone()), &(*right_any, right_types.clone()));
            }

            (TypeKind::Map(left_key, left_value), TypeKind::Map(right_key, right_value)) => {
                if !self.type_compare(left_key, right_key) {
                    return false;
                }
                if !self.type_compare(left_value, right_value) {
                    return false;
                }

                return true;
            }

            (TypeKind::Set(left_element), TypeKind::Set(right_element)) => self.type_compare(left_element, right_element),

            (TypeKind::Chan(left_element), TypeKind::Chan(right_element)) => self.type_compare(left_element, right_element),

            (TypeKind::Vec(left_element), TypeKind::Vec(right_element)) => self.type_compare(left_element, right_element),

            (TypeKind::Arr(_, left_len, left_element), TypeKind::Arr(_, right_len, right_element)) => {
                left_len == right_len && self.type_compare(left_element, right_element)
            }

            (TypeKind::Tuple(left_elements, _), TypeKind::Tuple(right_elements, _)) => {
                if left_elements.len() != right_elements.len() {
                    return false;
                }
                left_elements
                    .iter()
                    .zip(right_elements.iter())
                    .all(|(left, right)| self.type_compare(left, right))
            }

            (TypeKind::Fn(left_fn), TypeKind::Fn(right_fn)) => {
                if !self.type_compare(&left_fn.return_type, &right_fn.return_type)
                    || left_fn.param_types.len() != right_fn.param_types.len()
                    || left_fn.rest != right_fn.rest
                    || left_fn.errable != right_fn.errable
                {
                    return false;
                }

                left_fn
                    .param_types
                    .iter()
                    .zip(right_fn.param_types.iter())
                    .all(|(left, right)| self.type_compare(left, right))
            }

            (TypeKind::Struct(_, _, left_props), TypeKind::Struct(_, _, right_props)) => {
                if left_props.len() != right_props.len() {
                    return false;
                }
                left_props
                    .iter()
                    .zip(right_props.iter())
                    .all(|(left, right)| left.name == right.name && self.type_compare(&left.type_, &right.type_))
            }

            (TypeKind::Ptr(left_value), TypeKind::Ptr(right_value)) | (TypeKind::Rawptr(left_value), TypeKind::Rawptr(right_value)) => {
                self.type_compare(left_value, right_value)
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

            (TypeKind::Ptr(left_value), TypeKind::Ptr(right_value)) | (TypeKind::Rawptr(left_value), TypeKind::Rawptr(right_value)) => {
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
                    fndef.type_.ident = target_type.ident.clone();
                    fndef.type_.ident_kind = target_type.ident_kind.clone();
                    fndef.type_.args = target_type.args.clone();
                }

                return Ok(fndef.type_.clone());
            }

            fndef.clone()
        };

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
                temp.type_.clone()
            };

            // 对参数类型进行还原
            let mut param_type = self.reduction_type(param_type)?;
            debug_assert!(param_type.kind != TypeKind::Ident);

            // 为什么要在这里进行 ptr of, 只有在 infer 之后才能确定 alias 的具体类型，从而进一步判断是否需要 ptrof
            if fndef.impl_type.kind.is_exist() && i == 0 && param_type.is_stack_impl() {
                param_type = Type::ptr_of(param_type);
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
            param_types,
        })));

        if target_type.kind.is_exist() {
            result.ident = target_type.ident.clone();
            result.ident_kind = target_type.ident_kind.clone();
            result.args = target_type.args.clone();
        }

        {
            let mut temp_fn = fndef_mutex.lock().unwrap();
            temp_fn.type_ = result.clone();
        }

        Ok(result)
    }

    fn infer_global_vardef(&mut self, var_decl_mutex: &Arc<Mutex<VarDeclExpr>>, right_expr: &mut Box<Expr>) -> Result<(), AnalyzerError> {
        let mut var_decl = var_decl_mutex.lock().unwrap();
        var_decl.type_ = self.reduction_type(var_decl.type_.clone())?;

        let right_expr_type = self.infer_right_expr(right_expr, var_decl.type_.clone())?;

        if var_decl.type_.kind.is_unknown() {
            if !self.type_confirm(&right_expr_type) {
                return Err(AnalyzerError {
                    message: format!("global var {} type infer failed, right expr cannot confirm", var_decl.ident),
                    start: right_expr.start,
                    end: right_expr.end,
                });
            }
            var_decl.type_ = right_expr_type;
        }

        Ok(())
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

            // 不能包含 @ 符号
            if var_decl.ident.contains('@') {
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

    fn type_union_compare(&mut self, left: &(bool, Vec<Type>), right: &(bool, Vec<Type>)) -> bool {
        if left.0 == true {
            return true;
        }

        if right.0 && !left.0 {
            return false;
        }

        for right_type in right.1.iter() {
            if left.1.iter().find(|left_type| self.type_compare(left_type, right_type)).is_none() {
                return false;
            }
        }

        return true;
    }

    fn generics_constraints_compare(&mut self, left: &GenericsParam, right: &GenericsParam) -> bool {
        if left.constraints.1 {
            // any can match any type
            return true;
        }

        // any
        if right.constraints.1 && !left.constraints.1 {
            return false;
        }

        // and
        if right.constraints.2 && !left.constraints.2 {
            return false;
        }

        // or
        if right.constraints.3 && !left.constraints.3 {
            return false;
        }

        // 创建一个标记数组，用于标记left中的类型是否已经匹配
        // 遍历right中的类型，确保每个类型都存在于left中
        for right_type in &right.constraints.0 {
            let mut type_found = false;

            for left_type in &left.constraints.0 {
                if self.type_compare(left_type, right_type) {
                    type_found = true;
                    break;
                }
            }

            if !type_found {
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
            param_types: Vec::new(),
            return_type: self.reduction_type(fndef.return_type.clone())?,
        };

        // 跳过 self, self index = 1
        for (i, param) in fndef.params.iter().enumerate() {
            if i == 0 {
                continue;
            }
            let mut param_type = {
                let mut temp_param = param.lock().unwrap();
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

    fn generics_constrains_check(&mut self, param: &mut GenericsParam, src: &Type) -> Result<(), String> {
        let len = param.constraints.0.len();
        for constraint in &mut param.constraints.0 {
            match self.reduction_type(constraint.clone()) {
                Ok(t) => *constraint = t,
                Err(e) => return Err(e.message),
            }

            if len == 1 {
                if matches!(constraint.kind, TypeKind::Interface(..)) {
                    param.constraints.2 = true;
                } else if !matches!(constraint.kind, TypeKind::Union(..)) {
                    param.constraints.3 = true;
                }
            }

            if param.constraints.2 {
                if !matches!(constraint.kind, TypeKind::Interface(..)) {
                    return Err(format!("only type 'interface' can be combination with '&'"));
                }
            } else if param.constraints.3 {
                if matches!(constraint.kind, TypeKind::Union(..)) {
                    return Err(format!("cannot use type 'union' combination"));
                }
                if matches!(constraint.kind, TypeKind::Interface(..)) {
                    return Err(format!("cannot use type 'interface' combination"));
                }
            }
        }

        if param.constraints.1 {
            // any
            return Ok(());
        }

        // 处理 OR 约束
        if param.constraints.3 {
            for constraint in &param.constraints.0 {
                if self.type_compare(constraint, src) {
                    return Ok(());
                }
            }
            return Err(format!("type '{}' does not match any of the constraints", src));
        }

        // 处理接口约束 (AND)
        if param.constraints.2 {
            for constraint in &param.constraints.0 {
                if let TypeKind::Interface(elements) = &constraint.kind {
                    let temp_target_type = if matches!(src.kind, TypeKind::Ptr(_) | TypeKind::Rawptr(_)) {
                        match &src.kind {
                            TypeKind::Ptr(value_type) | TypeKind::Rawptr(value_type) => *value_type.clone(),
                            _ => {
                                continue;
                            }
                        }
                    } else {
                        src.clone()
                    };

                    // get symbol from symbol table
                    let symbol = match self.symbol_table.find_global_symbol(&temp_target_type.ident) {
                        Some(s) => s,
                        None => {
                            // 如果是内置类型且接口没有任何方法要求,则跳过检查
                            if elements.len() > 0 {
                                return Err(format!("type '{}' not impl '{}' interface", temp_target_type.ident, constraint));
                            }
                            continue;
                        }
                    };

                    if let SymbolKind::Type(typedef_mutex) = symbol.kind.clone() {
                        let typedef = typedef_mutex.lock().unwrap();
                        let found = self.check_impl_interface_contains(&typedef, constraint);

                        // 禁止制鸭子类型
                        if !found {
                            return Err(format!("type '{}' not impl '{}' interface", temp_target_type.ident, constraint));
                        }

                        self.check_typedef_impl(constraint, temp_target_type.ident.clone(), &typedef)?;
                    } else {
                        unreachable!();
                    }
                } else {
                    unreachable!();
                }
            }
            return Ok(());
        }

        // union check
        debug_assert!(param.constraints.0.len() == 1);
        let union_constraint = param.constraints.0[0].clone();
        if let TypeKind::Union(any, _, elements) = union_constraint.kind {
            if !self.union_type_contains(&(any, elements), src) {
                return Err(format!("type '{}' does not match any of the constraints", src));
            }
        } else {
            unreachable!();
        }

        return Ok(());
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

    fn infer_generics_param_constraints(&mut self, impl_type: Type, generics_params: &mut Vec<GenericsParam>) -> Result<(), String> {
        debug_assert!(impl_type.kind == TypeKind::Ident);
        debug_assert!(impl_type.ident_kind == TypeIdentKind::Def);

        let impl_ident = impl_type.ident.clone();

        let symbol_id = impl_type.symbol_id;
        if symbol_id == 0 {
            return Err(format!("type '{}' symbol not found", impl_ident));
        }
        let symbol = self
            .symbol_table
            .get_symbol(symbol_id)
            .ok_or_else(|| format!("typedef {} not found", impl_ident))?;

        let SymbolKind::Type(typedef_mutex) = symbol.kind.clone() else {
            return Err(format!("symbol '{}' is not typedef", impl_ident));
        };

        let mut typedef = typedef_mutex.lock().unwrap();
        // params.length == generics_params.length
        if typedef.params.len() != generics_params.len() {
            return Err(format!("typedef '{}' params length mismatch", impl_ident));
        }

        if generics_params.len() == 0 {
            return Ok(());
        }

        for (i, type_generics_param) in &mut typedef.params.iter_mut().enumerate() {
            // constraints
            for constraint in &mut type_generics_param.constraints.0 {
                *constraint = self.reduction_type(constraint.clone()).map_err(|e| e.message)?;
            }

            for constraint in type_generics_param.constraints.0.clone() {
                if type_generics_param.constraints.0.len() == 1 {
                    if matches!(constraint.kind, TypeKind::Interface(..)) {
                        type_generics_param.constraints.2 = true; // and
                    } else if !matches!(constraint.kind, TypeKind::Union(..)) {
                        type_generics_param.constraints.3 = true; // or
                    }
                }

                // 添加约束检查
                if type_generics_param.constraints.2 {
                    // 与组合(&)的情况
                    if !matches!(constraint.kind, TypeKind::Interface(..)) {
                        return Err(format!("only type 'interface' can be combination with '&'"));
                    }
                } else if type_generics_param.constraints.3 {
                    // 或组合(|)的情况
                    if matches!(constraint.kind, TypeKind::Union(..)) {
                        return Err(format!("cannot use type 'union' combination"));
                    }

                    if matches!(constraint.kind, TypeKind::Interface(..)) {
                        return Err(format!("cannot use type 'interface' combination"));
                    }
                }
            }

            let impl_generics_param = &mut generics_params[i];

            let compare = self.generics_constraints_compare(&type_generics_param, &impl_generics_param);
            if !compare {
                return Err(format!("type '{}' param constraint mismatch", impl_ident));
            }
        }

        Ok(())
    }

    /**
     * 返回基于类型参数组合 hash 值, 从而可以实现简单的函数重载, impl_type 存在则说明这是基于 impl 的泛型函数
     */
    pub fn generics_constraints_product(&mut self, impl_type: Type, generics_params: &mut Vec<GenericsParam>) -> Result<Vec<u64>, String> {
        let generics_params_len = generics_params.len();
        let mut hash_list: Vec<u64> = Vec::new();

        for param in &mut *generics_params {
            // for temp in &mut param.constraints.0 {
            //     *temp = self.reduction_type(temp.clone()).map_err(|e| e.message)?;
            // }
            let constraints_len = param.constraints.0.len();
            for temp in &mut param.constraints.0 {
                *temp = self.reduction_type(temp.clone()).map_err(|e| e.message)?;

                if constraints_len == 1 {
                    if matches!(temp.kind, TypeKind::Interface(..)) {
                        param.constraints.2 = true; // and
                    } else if !matches!(temp.kind, TypeKind::Union(..)) {
                        param.constraints.3 = true; // or
                    }
                }

                // 添加约束检查
                if param.constraints.2 {
                    // 与组合(&)的情况
                    if !matches!(temp.kind, TypeKind::Interface(..)) {
                        return Err(format!("only type 'interface' can be combination with '&'"));
                    }
                } else if param.constraints.3 {
                    // 或组合(|)的情况
                    if matches!(temp.kind, TypeKind::Union(..)) {
                        return Err(format!("cannot use type 'union' combination"));
                    }

                    if matches!(temp.kind, TypeKind::Interface(..)) {
                        return Err(format!("cannot use type 'interface' combination"));
                    }
                }
            }

            if param.constraints.1 {
                // 存在 any 则无法进行具体数量的组合约束
                return Ok(hash_list);
            }

            // and 表示这是 interface, 同样无法进行类型约束
            if param.constraints.2 {
                return Ok(hash_list);
            }
        }

        if Type::is_ident(&impl_type) {
            self.infer_generics_param_constraints(impl_type, generics_params)?
        }

        // 生成所有可能的类型组合
        let mut current_product = vec![Type::default(); generics_params_len];
        let mut combinations = Vec::new();

        // 递归生成笛卡尔积
        self.cartesian_product(generics_params, 0, &mut current_product, &mut combinations);

        // 为每个组合计算hash值
        for args_table in combinations {
            let hash = self.generics_args_hash(generics_params, args_table);
            hash_list.push(hash);
        }

        return Ok(hash_list);
    }

    // 辅助方法：生成笛卡尔积
    fn cartesian_product(&self, params: &Vec<GenericsParam>, depth: usize, current_product: &mut Vec<Type>, result: &mut Vec<HashMap<String, Type>>) {
        if depth == params.len() {
            // 创建新的参数表
            let mut arg_table = HashMap::new();
            for (i, param) in params.iter().enumerate() {
                arg_table.insert(param.ident.clone(), current_product[i].clone());
            }
            result.push(arg_table);
        } else {
            let param = &params[depth];
            // 遍历当前参数的所有可能类型
            for element_type in &param.constraints.0 {
                debug_assert!(element_type.status == ReductionStatus::Done);
                current_product[depth] = element_type.clone();
                self.cartesian_product(params, depth + 1, current_product, result);
            }
        }
    }

    pub fn pre_infer(&mut self) -> Vec<AnalyzerError> {
        //  - Global variables also contain type information, which needs to be restored and derived
        let mut vardefs = std::mem::take(&mut self.module.global_vardefs);
        for node in &mut vardefs {
            let AstNode::VarDef(var_decl_mutex, right_expr) = node else { unreachable!() };

            if let Err(e) = self.infer_global_vardef(var_decl_mutex, right_expr) {
                self.errors_push(e.start, e.end, e.message);
            }
        }
        self.module.global_vardefs = vardefs;

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
            } else {
                let mut fndef = fndef_mutex.lock().unwrap();
                debug_assert!(fndef.generics_params.is_some());

                let scope_id = self.module.scope_id;

                // 基于泛型组合进行符号注册到符号表中，但是不进行具体的函数生成，以及 type param 展开，仅仅是注册到符号表的 key 有所不同
                // 只有当具体的 call 调用批评到具体的函数时，才会进行生成
                match self.generics_constraints_product(fndef.impl_type.clone(), fndef.generics_params.as_mut().unwrap()) {
                    Ok(generics_args) => {
                        if generics_args.len() == 0 {
                            // 作为泛型函数，但是没有任何泛型参数约束, 直接使用原始名称注册即可
                            // fndef.symbol_name 在 sem 阶段进行了 module ident 拼接，所以注册到符号表中的 global fn 使用了完整名称
                            debug!("symbol {} will register to table, params_len {}", fndef.symbol_name, fndef.params.len());

                            let _ = self.symbol_table.define_symbol_in_scope(
                                fndef.symbol_name.clone(),
                                SymbolKind::Fn(fndef_mutex.clone()),
                                fndef.symbol_start,
                                scope_id,
                            );
                        } else {
                            for arg_hash in generics_args {
                                // new symbol_name  symbol_name@arg_hash in symbol_table
                                let symbol_name = format_generics_ident(fndef.symbol_name.clone(), arg_hash);

                                // 由于存在重载， fndef 的 symbol_id 可能注册冲突导致失败，此时全部换新成 symbol_id
                                // 多个 symbol_name 会共用同一个 symbol_id, 覆盖为最后一个即可

                                // 仅仅注册了 symbol hash 符号，此时 data 还是原始 fndef_mutex
                                debug!("gen arg symbol {} will register to table, params_len {}", symbol_name, fndef.params.len());
                                match self
                                    .symbol_table
                                    .define_symbol_in_scope(symbol_name, SymbolKind::Fn(fndef_mutex.clone()), fndef.symbol_start, scope_id)
                                {
                                    Ok(symbol_id) => {
                                        fndef.symbol_id = symbol_id;
                                    }
                                    Err(_e) => {
                                        // 与次同时，可以进行泛型的冲突检测, 避免相同的类型约束重复声明
                                        self.errors_push(
                                            fndef.symbol_start,
                                            fndef.symbol_end,
                                            format!("generics fn {} param constraint redeclared", fndef.symbol_name),
                                        );
                                    }
                                }
                            }
                        }
                    }
                    Err(e) => {
                        self.errors_push(fndef.symbol_start, fndef.symbol_end, e);
                    }
                }
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
