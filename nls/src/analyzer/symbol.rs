use log::{debug, trace};

use crate::utils::format_global_ident;

use super::common::{AstFnDef, TypedefStmt, VarDeclExpr, AstConstDef};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};

// 定义索引类型
pub type NodeId = usize;

// Arena 分配器
#[derive(Debug, Clone)]
pub struct Arena<T> {
    items: HashMap<NodeId, T>,
    next_id: NodeId,
}

impl<T> Arena<T> {
    fn new() -> Self {
        Arena {
            items: HashMap::new(),
            next_id: 1,
        }
    }

    fn alloc(&mut self, value: T) -> NodeId {
        let id = self.next_id;
        self.items.insert(id, value);
        self.next_id += 1;
        id
    }

    fn get(&self, id: NodeId) -> Option<&T> {
        self.items.get(&id)
    }

    fn get_mut(&mut self, id: NodeId) -> Option<&mut T> {
        self.items.get_mut(&id)
    }

    fn remove(&mut self, id: NodeId) {
        self.items.remove(&id);
    }
}

//  引用自 AstNode
#[derive(Debug, Clone)]
pub enum SymbolKind {
    Var(Arc<Mutex<VarDeclExpr>>), // 变量原始定义
    Fn(Arc<Mutex<AstFnDef>>),
    Type(Arc<Mutex<TypedefStmt>>),
    Const(Arc<Mutex<AstConstDef>>),
}

// symbol table 可以同时处理多个文件的 scope, 存在一个 global scope 管理所有的全局 scope, 符号注册到 global scope 时，define_ident 需要携带 package_name 保证符号的唯一性
#[derive(Debug, Clone)]
pub struct Symbol {
    // local symbol 直接使用，global symbol 会携带 package ident
    pub ident: String,
    pub kind: SymbolKind,
    pub defined_in: NodeId, // defined in scope
    pub is_local: bool, // 是否是 module 级别的 global symbol
    pub pos: usize,         // 符号定义的起始

    // local symbol 需要一些额外信息
    pub is_capture: bool, // 如果变量被捕获，则需要分配到堆中，避免作用域问题

    pub generics_id_map: HashMap<String, NodeId>, // new ident -> new symbol id
}

#[derive(Debug, Clone)]
pub struct FreeIdent {
    pub in_parent_local: bool,   // 是否在父作用域中直接定义, 否则需要通过 envs[index] 访问
    pub parent_env_index: usize, // 父作用域起传递作用, 通过 envs 参数向下传递

    pub index: usize, // free in frees index
    pub ident: String,
    pub kind: SymbolKind,
}

#[derive(Debug, Clone)]
pub enum ScopeKind {
    // 全局作用域，每个 module scope 中的符号会进行改名并注册到全局作用域中，module 清理时需要清理对应的 global scope
    // buitin 直接注册到全局作用域中
    Global,
    Module(String),  // 创建 module 产生的 scope, 当前 scope 中存储了当前 module 的所有符号
    GlobalFn(Arc<Mutex<AstFnDef>>),
    LocalFn(Arc<Mutex<AstFnDef>>),
    Local,
}

#[derive(Debug, Clone)]
pub struct Scope {
    pub parent: NodeId,              // 除了全局作用域外，每个作用域都有一个父作用域
    pub symbols: Vec<NodeId>,                // 当前作用域中定义的符号列表

    pub children: Vec<NodeId>,               // 子作用域列表
    pub symbol_map: HashMap<String, NodeId>, // 符号名到符号ID的映射

    pub range: (usize, usize), // 作用域的范围, [start, end)

    // 当前作用域是否为函数级别作用域
    pub kind: ScopeKind,

    pub frees: HashMap<String, FreeIdent>, // fn scope 需要处理函数外的自由变量
}

#[derive(Debug, Clone)]
pub struct SymbolTable {
    pub scopes: Arena<Scope>,     // 所有的作用域列表, 根据 NodeId 索引
    pub symbols: Arena<Symbol>,   // 所有的符号列表, 根据 NodeId 索引
    pub module_scopes: HashMap<String, NodeId>, // 不再创建 global scope, symbol_table 就是 global scope
    pub global_scope_id: NodeId,
}

impl SymbolTable {
    pub fn new() -> Self {
        // 创建 global scope
        let mut result = Self{
            scopes: Arena::new(),
            symbols: Arena::new(),
            module_scopes: HashMap::new(),
            global_scope_id: 0,
        };

        result.global_scope_id = result.create_scope(ScopeKind::Global, 0, 0, 0);

        result
    }

    pub fn find_scope(&self, scope_id: NodeId) -> &Scope {
        self.scopes.get(scope_id).unwrap()
    }

    // 创建新的作用域
    pub fn create_scope(&mut self, kind: ScopeKind, parent_id: NodeId, start: usize, end: usize) -> NodeId {
        let new_scope = Scope {
            parent: parent_id,
            symbols: Vec::new(),
            children: Vec::new(),
            symbol_map: HashMap::new(),
            range: (start, end),
            kind,
            frees: HashMap::new(),
        };

        let new_scope_id = self.scopes.alloc(new_scope);

        // 将新作用域添加到父作用域的children中, current cope 作为 parent
        if  parent_id > 0 {
            if let Some(parent_scope) = self.scopes.get_mut(parent_id) {
                parent_scope.children.push(new_scope_id);
            }
        }

        new_scope_id
    }

    /**
     * 返回 current scope 的 parent scope id
     */
    pub fn exit_scope(&mut self, scope_id: NodeId) -> NodeId {
        if let Some(scope) = self.scopes.get_mut(scope_id) {
            debug_assert!(scope.parent > 0, "module scope cannot exit");

            return scope.parent;
        }

        panic!("scope not found");
    }

    /**
     * 如果 module_ident == "", 直接使用 global scope 即可
     */
    pub fn create_module_scope(&mut self, moudel_ident: String) -> NodeId {
        if moudel_ident == "" {
            return self.global_scope_id;
        }


        if let Some(_scope_id) = self.module_scopes.get(&moudel_ident) {
            panic!("module scope already exists");
            // return *scope_id;
        }

        let scope_id = self.create_scope(ScopeKind::Module(moudel_ident.clone()), self.global_scope_id, 0, 0);
        self.module_scopes.insert(moudel_ident, scope_id);
        scope_id
    }

    pub fn clean_module_scope(&mut self, module_ident: String) {
        if module_ident == "" {
            return;
        }

        let module_scope_id = self.module_scopes.get(&module_ident).unwrap();
        let module_scope = self.scopes.get_mut(*module_scope_id).unwrap();
        let module_symbol_map = module_scope.symbol_map.clone();

        // 基于 worklist 的方式，从 arena 中清理所有的 symbol 和 children scope?
        let mut worklist = Vec::new();
        worklist.extend(module_scope.children.clone());

        // module scope 本身不进行清理, 所以需要手动清空其中的字段
        module_scope.symbols = Vec::new();
        module_scope.children = Vec::new();
        module_scope.symbol_map = HashMap::new();

        // module scope 中的 global symbol 冗余注册到了 global scope 中，需要进行删除清理
        let global_scope = self.scopes.get_mut(self.global_scope_id).unwrap();

        // module_scope 中的 symbol 都是 global symbol, 需要从 global scope 中清理
        for (ident, module_symbol_id) in module_symbol_map.iter() {

            let symbol_id_option = global_scope.symbol_map.get(ident);
            if let Some(global_symbol_id) = symbol_id_option {
                debug_assert!(global_symbol_id != module_symbol_id);

                self.symbols.remove(*global_symbol_id);
                global_scope.symbol_map.remove(ident);
            }


            self.symbols.remove(*module_symbol_id);
        }

        // 处理 worklist
        while let Some(scope_id) = worklist.pop() {
            if let Some(scope) = self.scopes.get(scope_id) {
                // 将当前作用域的子作用域添加到工作列表
                worklist.extend(scope.children.clone());

                // 将当前作用域中的所有符号从 local scope 中清理
                for symbol_id in &scope.symbols {
                    self.symbols.remove(*symbol_id);
                }
            }

            self.scopes.remove(scope_id);
        }
    }

    pub fn define_global_symbol(&mut self, global_ident: String, kind: SymbolKind, pos: usize, defined_in: NodeId) -> Result<NodeId, String> {
        debug_assert!(global_ident != "");

        // 注册到 global scope
        let global_scope = self.scopes.get_mut(self.global_scope_id).unwrap();

        if let Some(&_) = global_scope.symbol_map.get(&global_ident) {
            // 获取已存在的符号
            return Err(format!("redeclare global ident '{}'", global_ident));
        }

        let symbol = Symbol {
            ident: global_ident.clone(),
            kind,
            defined_in, // global ident 的 defined_in 指向 module scope id
            is_local: false,
            pos,
            is_capture: false,
            generics_id_map: HashMap::new(),
        };

        let global_symbol_id = self.symbols.alloc(symbol);

        // 将符号添加到当前作用域
        // scope.symbols.push(symbol_id); // 为了方便清理, 不添加到 symbols  中
        global_scope.symbol_map.insert(global_ident.clone(), global_symbol_id);

        // debug!("define global symbol {}, global symbol_id: {}, defined_in_scope_id: {}", &global_ident, global_symbol_id, defined_in);

        Ok(global_symbol_id)
    }

    pub fn cover_symbol_in_scope(&mut self, ident: String, kind: SymbolKind, pos: usize, scope_id: NodeId) -> NodeId {
        // 检查当前作用域是否已存在同名符号
        let scope = self.scopes.get_mut(scope_id).unwrap();

        if let Some(&symbol_id) = scope.symbol_map.get(&ident) {
            // 获取已存在的符号, 则读取符号内容进行修改
            let symbol =  self.get_symbol(symbol_id).unwrap();
            symbol.kind = kind;
            symbol.pos = pos;
            symbol.generics_id_map = HashMap::new();
            return symbol_id;
        }

        // 符号不存在，直接调用 define_symbol_in_scope 创建
        return self.define_symbol_in_scope(ident, kind, pos, scope_id).unwrap();
    }

    pub fn define_symbol_in_scope(&mut self, ident: String, kind: SymbolKind, pos: usize, scope_id: NodeId) -> Result<NodeId, String> {
        debug_assert!(ident != "");

        // 检查当前作用域是否已存在同名符号
        let scope = self.scopes.get_mut(scope_id).unwrap();

        let is_local = !matches!(scope.kind, ScopeKind::Module(..)|ScopeKind::Global);

        if let Some(&_) = scope.symbol_map.get(&ident) {
            // 获取已存在的符号
            return Err(format!("redeclare ident '{}'", ident));
        }

        let symbol = Symbol {
            ident: ident.clone(),
            kind,
            defined_in: scope_id,
            is_local,
            pos,
            is_capture: false,
            generics_id_map: HashMap::new(),
        };

        if let SymbolKind::Fn(fn_mutex) = &symbol.kind {
            trace!("define fn symbol {}, fn_mutex_ptr: {:?}",  &ident, Arc::as_ptr(fn_mutex));
        }

        let symbol_id = self.symbols.alloc(symbol);

        // 将符号添加到当前作用域
        if let Some(scope) = self.scopes.get_mut(scope_id) {
            scope.symbols.push(symbol_id);
            scope.symbol_map.insert(ident.clone(), symbol_id);
        }

        // debug!("define symbol {}, module symbol_id: {}, defined_in_scope_id: {}",  &ident, symbol_id, scope_id);

        Ok(symbol_id)
    }

    // 查找符号 from current scope（包括父作用域）
    pub fn lookup_symbol(&self, ident: &str, current_scope_id: NodeId) -> Option<NodeId> {
        let mut current = current_scope_id;

        while current > 0 {
            if let Some(scope) = self.scopes.get(current) {
                // 在当前作用域中查找
                if let Some(&symbol_id) = scope.symbol_map.get(ident) {
                    return Some(symbol_id);
                }
                // 继续查找父作用域
                current = scope.parent;
            } else {
                break;
            }
        }
        None
    }

    /**
     * 从当前 scope 向上查找，直到找到一个 global fn 类型的 scope, 从而构建 global fn 和 local fn 的关系
     */
    pub fn find_global_fn(&self, current_scope_id: NodeId) -> Option<Arc<Mutex<AstFnDef>>> {
        let mut current = current_scope_id;

        while current > 0 {
            if let Some(scope) = self.scopes.get(current) {
                match &scope.kind {
                    ScopeKind::GlobalFn(fn_def) => return Some(fn_def.clone()),
                    _ => current = scope.parent,
                }
            } else {
                break;
            }
        }
        None
    }

    pub fn find_symbol_id(&self, ident: &str, scope_id: NodeId) -> Option<NodeId> {
        if let Some(scope) = self.scopes.get(scope_id) {
            return scope.symbol_map.get(ident).cloned();
        } else {
            return None;
        }
    }

    pub fn find_scope_id(&self, symbol_id: NodeId) -> NodeId {
        if let Some(symbol) = self.symbols.get(symbol_id) {
            return symbol.defined_in;
        }

        panic!("symbol not found")
    }

    pub fn find_module_symbol_id(&self, module_ident: &str, ident: &str) -> Option<NodeId> {
        if let Some(scope_id) = self.module_scopes.get(module_ident) {
            let global_ident = format_global_ident(module_ident.to_string(), ident.to_string());
            return self.find_symbol_id(&global_ident, *scope_id);
        } else {
            return None;
        }
    }

    pub fn find_global_symbol(&self, global_ident: &str) -> Option<&Symbol> {
        let global_scope = self.scopes.get(self.global_scope_id).unwrap();
        if let Some(&symbol_id) = global_scope.symbol_map.get(global_ident) {
            return Some(self.symbols.get(symbol_id).unwrap());
        } else {
            return None;
        }
    }

    pub fn find_symbol(&self, ident: &str, scope_id: NodeId) -> Option<&Symbol> {
        if let Some(symbol_id) = self.find_symbol_id(ident, scope_id) {
            return Some(self.symbols.get(symbol_id).unwrap());
        } else {
            return None;
        }
    }

    pub fn symbol_exists_in_scope(&self, ident: &str, scope_id: NodeId) -> bool {
        if let Some(scope) = self.scopes.get(scope_id) {
            return scope.symbol_map.contains_key(ident);
        } else {
            return false;
        }
    }

    pub fn get_symbol(&mut self, id: NodeId) -> Option<&mut Symbol> {
        self.symbols.get_mut(id)
    }

    pub fn get_symbol_ref(&self, id: NodeId) -> Option<&Symbol> {
        self.symbols.get(id)
    }

    // 打印作用域树（用于调试）
    pub fn print_scope_tree(&self, scope_id: NodeId, indent: usize) {
        if let Some(scope) = self.scopes.get(scope_id) {
            debug!("{}Scope {:?}:", " ".repeat(indent), scope_id);

            // 打印该作用域中的符号
            for &symbol_id in &scope.symbols {
                if let Some(symbol) = self.symbols.get(symbol_id) {
                    debug!("{}  Symbol: {} ({:?})", " ".repeat(indent), symbol.ident, symbol.kind);
                }
            }

            // 递归打印子作用域
            for &child_id in &scope.children {
                self.print_scope_tree(child_id, indent + 2);
            }
        }
    }
}
