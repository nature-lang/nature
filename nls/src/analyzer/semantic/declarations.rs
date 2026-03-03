use log::debug;

use crate::utils::errors_push;

use super::super::common::*;
use super::super::symbol::{ScopeKind, SymbolKind};
use super::Semantic;
use std::sync::{Arc, Mutex};

impl<'a> Semantic<'a> {
    pub fn analyze_global_fn(&mut self, fndef_mutex: Arc<Mutex<AstFnDef>>) {
        {
            let mut fndef = fndef_mutex.lock().unwrap();

            fndef.is_local = false;
            fndef.module_index = self.module.index;
            if fndef.generics_params.is_some() {
                fndef.is_generics = true;
            }

            if fndef.is_tpl && fndef.body.stmts.len() == 0 {
                debug_assert!(fndef.body.stmts.len() == 0);
            }

            self.analyze_type(&mut fndef.return_type);
            // Resolve impl_type so its ident becomes module-qualified (e.g. "module.Dog")
            // and symbol_id is set. This is required for symbol_typedef_add_method to
            // locate the typedef in the global scope.
            if fndef.impl_type.kind.is_exist() {
                self.analyze_type(&mut fndef.impl_type);
            }

            // 如果 impl type 是 type alias, 则从符号表中获取当前的 type alias 的全称进行更新
            // fn vec<T>.len() -> fn vec_len(vec<T> self)
            // impl 是 type alias 时，只能是 fn person_t.len() 而不能是 fn pkg.person_t.len()
            if fndef.impl_type.kind.is_exist() {
                if !fndef.is_static && fndef.self_kind != SelfKind::Null {
                    // 重构 params 的位置, 新增 self param
                    let mut new_params = Vec::new();
                    let param_type = fndef.impl_type.clone();
                    let self_vardecl = VarDeclExpr {
                        ident: String::from("self"),
                        type_: param_type,
                        be_capture: false,
                        heap_ident: None,
                        symbol_start: fndef.symbol_start,
                        symbol_end: fndef.symbol_end,
                        symbol_id: 0,
                    };

                    new_params.push(Arc::new(Mutex::new(self_vardecl)));
                    new_params.extend(fndef.params.iter().cloned());
                    fndef.params = new_params;

                    // builtin type 没有注册在符号表，不能添加 method
                    if !Type::is_impl_builtin_type(&fndef.impl_type.kind) {
                        self.symbol_typedef_add_method(fndef.impl_type.ident.clone(), fndef.symbol_name.clone(), fndef_mutex.clone())
                            .unwrap_or_else(|e| {
                                errors_push(
                                    self.module,
                                    AnalyzerError {
                                        start: fndef.symbol_start,
                                        end: fndef.symbol_end,
                                        message: e,
                                        is_warning: false,
                                    },
                                );
                            });
                    }
                }
            }

            self.enter_scope(ScopeKind::GlobalFn(fndef_mutex.clone()), fndef.symbol_start, fndef.symbol_end);

            // 函数形参处理
            for param_mutex in &fndef.params {
                let mut param = param_mutex.lock().unwrap();
                self.analyze_type(&mut param.type_);

                // 将参数添加到符号表中
                match self.symbol_table.define_symbol_in_scope(
                    param.ident.clone(),
                    SymbolKind::Var(param_mutex.clone()),
                    param.symbol_start,
                    self.current_scope_id,
                ) {
                    Ok(symbol_id) => {
                        param.symbol_id = symbol_id;
                    }
                    Err(e) => {
                        errors_push(
                            self.module,
                            AnalyzerError {
                                start: param.symbol_start,
                                end: param.symbol_end,
                                message: e,
                                is_warning: false,
                            },
                        );
                    }
                }
            }
        }

        {
            let mut body = {
                let mut fndef = fndef_mutex.lock().unwrap();
                std::mem::take(&mut fndef.body)
            };

            if body.stmts.len() > 0 {
                self.analyze_body(&mut body);
            }

            // 将当前的 fn 添加到 global fn 的 local_children 中
            {
                let mut fndef = fndef_mutex.lock().unwrap();

                // 归还 body
                fndef.body = body;
                fndef.local_children = self.current_local_fn_list.clone();
            }
        }

        // 清空 self.current_local_fn_list, 进行重新计算
        self.current_local_fn_list.clear();

        self.exit_scope();
    }

    /**
     * local fn in global fn
     */
    pub fn analyze_local_fndef(&mut self, fndef_mutex: &Arc<Mutex<AstFnDef>>) {
        self.module.all_fndefs.push(fndef_mutex.clone());

        let mut fndef = fndef_mutex.lock().unwrap();

        // find global fn in symbol table
        let Some(global_fn_mutex) = self.symbol_table.find_global_fn(self.current_scope_id) else {
            return;
        };
        fndef.global_parent = Some(global_fn_mutex.clone());
        fndef.is_local = true;

        self.current_local_fn_list.push(fndef_mutex.clone());

        // local fn 作为闭包函数, 不能进行类型扩展和泛型参数
        if fndef.impl_type.kind.is_exist() || fndef.generics_params.is_some() {
            errors_push(
                self.module,
                AnalyzerError {
                    start: fndef.symbol_start,
                    end: fndef.symbol_end,
                    message: "closure fn cannot be generics or impl type alias".to_string(),
                    is_warning: false,
                },
            );
        }

        // 闭包不能包含 macro ident
        if fndef.linkid.is_some() {
            errors_push(
                self.module,
                AnalyzerError {
                    start: fndef.symbol_start,
                    end: fndef.symbol_end,
                    message: "closure fn cannot have #linkid label".to_string(),
                    is_warning: false,
                },
            );
        }

        if fndef.is_tpl {
            errors_push(
                self.module,
                AnalyzerError {
                    start: fndef.symbol_start,
                    end: fndef.symbol_end,
                    message: "closure fn cannot be template".to_string(),
                    is_warning: false,
                },
            );
        }

        self.analyze_type(&mut fndef.return_type);

        self.enter_scope(ScopeKind::LocalFn(fndef_mutex.clone()), fndef.symbol_start, fndef.symbol_end);

        // 形参处理
        for param_mutex in &fndef.params {
            let mut param = param_mutex.lock().unwrap();
            self.analyze_type(&mut param.type_);

            // 将参数添加到符号表中
            match self.symbol_table.define_symbol_in_scope(
                param.ident.clone(),
                SymbolKind::Var(param_mutex.clone()),
                param.symbol_start,
                self.current_scope_id,
            ) {
                Ok(symbol_id) => {
                    param.symbol_id = symbol_id;
                }
                Err(e) => {
                    errors_push(
                        self.module,
                        AnalyzerError {
                            start: param.symbol_start,
                            end: param.symbol_end,
                            message: e,
                            is_warning: false,
                        },
                    );
                }
            }
        }

        // handle body
        self.analyze_body(&mut fndef.body);

        let mut free_var_count = 0;
        let scope = self.symbol_table.find_scope(self.current_scope_id);
        for (_, free_ident) in scope.frees.iter() {
            if matches!(free_ident.kind, SymbolKind::Var(..)) {
                free_var_count += 1;
            }
        }

        self.exit_scope();

        // 当前函数需要编译成闭包, 所有的 call fn 改造成 call fn_var
        if free_var_count > 0 {
            fndef.is_closure = true;
        }

        // 将 fndef lambda 添加到 symbol table 中
        match self.symbol_table.define_symbol_in_scope(
            fndef.symbol_name.clone(),
            SymbolKind::Fn(fndef_mutex.clone()),
            fndef.symbol_start,
            self.current_scope_id,
        ) {
            Ok(symbol_id) => {
                fndef.symbol_id = symbol_id;
            }
            Err(e) => {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: fndef.symbol_start,
                        end: fndef.symbol_end,
                        message: e,
                        is_warning: false,
                    },
                );
            }
        }
    }

    /// Infer variable type from right-hand expression when type is unknown.
    /// Handles function calls by looking up the called function's return type.
    pub(crate) fn infer_var_type_from_expr(&mut self, var_decl_mutex: &Arc<Mutex<VarDeclExpr>>, expr: &Box<Expr>) {
        let needs_inference = {
            let var_decl = var_decl_mutex.lock().unwrap();
            var_decl.type_.kind.is_unknown()
        };

        if !needs_inference {
            return;
        }

        // Try to infer type from the expression
        if let Some(mut inferred_type) = self.infer_type_from_expr(expr) {
            // Resolve unresolved type idents (e.g., return type "testing" that hasn't been analyzed yet)
            self.resolve_inferred_type(&mut inferred_type);

            debug!("Inferred type for variable: {:?}, symbol_id: {}", inferred_type.kind, inferred_type.symbol_id);
            let mut var_decl = var_decl_mutex.lock().unwrap();
            var_decl.type_ = inferred_type;
        }
    }

    /// Resolve an unresolved type ident in an inferred type.
    /// Handles direct Ident types and types wrapped in Ref/Ptr.
    fn resolve_inferred_type(&mut self, t: &mut Type) {
        use crate::analyzer::common::TypeKind;

        // Handle unreduced ref<T>/ptr<T> stored as TypeKind::Ident with args.
        // Convert them to proper TypeKind::Ref/Ptr before resolving.
        if t.kind == TypeKind::Ident
            && !t.args.is_empty()
            && (t.ident == "ref" || t.ident == "ptr")
        {
            let mut inner = t.args.remove(0);
            self.resolve_inferred_type(&mut inner);
            if t.ident == "ref" {
                t.kind = TypeKind::Ref(Box::new(inner.clone()));
            } else {
                t.kind = TypeKind::Ptr(Box::new(inner.clone()));
            }
            // Propagate symbol_id from inner for completion/hover lookup
            if t.symbol_id == 0 && inner.symbol_id != 0 {
                t.symbol_id = inner.symbol_id;
                t.ident = inner.ident.clone();
                if t.ident_kind == TypeIdentKind::Unknown {
                    t.ident_kind = inner.ident_kind.clone();
                }
            }
            return;
        }

        match &mut t.kind {
            TypeKind::Ident => {
                if t.symbol_id == 0 && !t.ident.is_empty() {
                    if let Some(symbol_id) = self.resolve_typedef(&mut t.ident) {
                        t.symbol_id = symbol_id;
                        if t.ident_kind == TypeIdentKind::Unknown {
                            t.ident_kind = TypeIdentKind::Def;
                        }
                    } else {
                        // Fallback: try direct global lookup (ident may already be
                        // fully qualified from a cross-module return type).
                        if let Some(symbol_id) = self.symbol_table.find_symbol_id(&t.ident, self.symbol_table.global_scope_id) {
                            t.symbol_id = symbol_id;
                            if t.ident_kind == TypeIdentKind::Unknown {
                                t.ident_kind = TypeIdentKind::Def;
                            }
                        } else {
                            // Last resort: suffix match against all global symbols.
                            let scope = self.symbol_table.find_scope(self.symbol_table.global_scope_id);
                            for (name, &sym_id) in &scope.symbol_map {
                                if name.rsplit('.').next() == Some(&t.ident) {
                                    if let Some(sym) = self.symbol_table.get_symbol_ref(sym_id) {
                                        if matches!(sym.kind, SymbolKind::Type(_)) {
                                            t.symbol_id = sym_id;
                                            t.ident = name.clone();
                                            if t.ident_kind == TypeIdentKind::Unknown {
                                                t.ident_kind = TypeIdentKind::Def;
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            TypeKind::Ref(inner) | TypeKind::Ptr(inner) => {
                self.resolve_inferred_type(inner);
                // Propagate the resolved symbol_id to the outer type for completion lookup
                if t.symbol_id == 0 && inner.symbol_id != 0 {
                    t.symbol_id = inner.symbol_id;
                    t.ident = inner.ident.clone();
                    if t.ident_kind == TypeIdentKind::Unknown {
                        t.ident_kind = inner.ident_kind.clone();
                    }
                }
            }
            _ => {}
        }
    }

    /// Try to infer a Type from an expression node.
    fn infer_type_from_expr(&self, expr: &Box<Expr>) -> Option<Type> {
        match &expr.node {
            AstNode::Call(call) => {
                // Look up the function being called to get its return type
                self.infer_type_from_call(call)
            }
            AstNode::New(type_, _, _) | AstNode::StructNew(_, type_, _) => {
                // new Type{} or Type{} — the type is the type being constructed
                Some(type_.clone())
            }
            AstNode::Ident(_, symbol_id) => {
                // Variable reference — look up the variable's type
                if *symbol_id != 0 {
                    if let Some(symbol) = self.symbol_table.get_symbol_ref(*symbol_id) {
                        match &symbol.kind {
                            SymbolKind::Var(var) => {
                                let var = var.lock().unwrap();
                                if !var.type_.kind.is_unknown() {
                                    return Some(var.type_.clone());
                                }
                            }
                            SymbolKind::Const(c) => {
                                let c = c.lock().unwrap();
                                if !c.type_.kind.is_unknown() {
                                    return Some(c.type_.clone());
                                }
                            }
                            _ => {}
                        }
                    }
                }
                None
            }
            _ => None,
        }
    }

    /// Infer type from a function call by looking up the function's return type.
    fn infer_type_from_call(&self, call: &AstCall) -> Option<Type> {
        // The called function is typically an Ident or a SelectExpr (module.fn_name)
        match &call.left.node {
            AstNode::Ident(_, symbol_id) => {
                if *symbol_id != 0 {
                    if let Some(symbol) = self.symbol_table.get_symbol_ref(*symbol_id) {
                        if let SymbolKind::Fn(fndef) = &symbol.kind {
                            let fndef = fndef.lock().unwrap();
                            if !fndef.return_type.kind.is_unknown() {
                                return Some(fndef.return_type.clone());
                            }
                        }
                    }
                }
                None
            }
            AstNode::SelectExpr(left, key, _) => {
                // module.fn_name() — look up the function in the module's scope
                if let AstNode::Ident(module_name, _) = &left.node {
                    // Find the module import
                    if let Some(import) = self.imports.iter().find(|i| i.as_name == *module_name) {
                        let global_ident = crate::utils::format_global_ident(import.module_ident.clone(), key.clone());
                        if let Some(symbol_id) = self.symbol_table.find_symbol_id(&global_ident, self.symbol_table.global_scope_id) {
                            if let Some(symbol) = self.symbol_table.get_symbol_ref(symbol_id) {
                                if let SymbolKind::Fn(fndef) = &symbol.kind {
                                    let fndef = fndef.lock().unwrap();
                                    if !fndef.return_type.kind.is_unknown() {
                                        return Some(fndef.return_type.clone());
                                    }
                                }
                            }
                        }
                    }
                }
                None
            }
            // StructSelect: instance.method() — look up the method's return type
            AstNode::StructSelect(_, _, _) => None,
            _ => None,
        }
    }

    pub fn analyze_var_decl(&mut self, var_decl_mutex: &Arc<Mutex<VarDeclExpr>>) {
        let mut var_decl = var_decl_mutex.lock().unwrap();

        self.analyze_type(&mut var_decl.type_);

        // 添加到符号表，返回值 sysmbol_id 添加到 var_decl 中, 已经包含了 redeclare check
        match self.symbol_table.define_symbol_in_scope(
            var_decl.ident.clone(),
            SymbolKind::Var(var_decl_mutex.clone()),
            var_decl.symbol_start,
            self.current_scope_id,
        ) {
            Ok(symbol_id) => {
                var_decl.symbol_id = symbol_id;
            }
            Err(e) => {
                errors_push(
                    self.module,
                    AnalyzerError {
                        start: var_decl.symbol_start,
                        end: var_decl.symbol_end,
                        message: e,
                        is_warning: false,
                    },
                );
            }
        }
    }
}
