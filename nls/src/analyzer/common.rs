use std::collections::HashMap;
use std::fmt::{Display, Formatter, Result};
use std::hash::{DefaultHasher, Hash, Hasher};
use std::sync::{Arc, Mutex};
use strum_macros::Display;

use crate::utils::align_up;

use super::symbol::NodeId;

use serde::Deserialize;

#[derive(Deserialize, Debug, Clone)]
pub struct LinkTargets {
    pub linux_amd64: Option<String>,
    pub linux_arm64: Option<String>,
    pub darwin_amd64: Option<String>,
    pub darwin_arm64: Option<String>,
}

#[derive(Deserialize, Debug, Clone)]
pub struct Dependency {
    #[serde(rename = "type")]
    pub dep_type: String, // git or local
    pub version: String,
    pub url: Option<String>,
    pub path: Option<String>,
}

#[derive(Deserialize, Debug, Clone)]
pub struct PackageData {
    pub name: String,
    pub version: String,
    pub entry: Option<String>, // custom entry point, default main
    pub authors: Option<Vec<String>>,
    pub description: Option<String>,
    pub license: Option<String>,
    #[serde(rename = "type")]
    pub package_type: String, // 因为 type 是关键字，所以重命名
    pub links: Option<HashMap<String, LinkTargets>>,
    #[serde(default)]
    pub dependencies: HashMap<String, Dependency>,
}

#[derive(Debug, Clone)]
pub struct PackageConfig {
    pub path: String,
    pub package_data: PackageData,
}

#[derive(Debug, Clone)]
pub struct AnalyzerError {
    pub start: usize,
    pub end: usize,
    pub message: String,
}

#[derive(Debug, Clone)]
pub struct Type {
    pub import_as: String,
    pub ident: String,
    pub symbol_id: NodeId,
    pub args: Vec<Type>, // type def 和 type impl 都存在 args，共用该 args 字段
    pub ident_kind: TypeIdentKind,
    pub kind: TypeKind,
    pub status: ReductionStatus,
    pub start: usize, // 类型定义开始位置
    pub end: usize,   // 类型定义结束位置
    pub in_heap: bool,
    pub err: bool,
}

impl Default for Type {
    fn default() -> Self {
        Self {
            kind: TypeKind::Unknown,
            status: ReductionStatus::Done,
            import_as: "".to_string(),
            ident: "".to_string(),
            symbol_id: 0,
            args: Vec::new(),
            ident_kind: TypeIdentKind::Unknown,
            start: 0,
            end: 0,
            in_heap: false,
            err: false,
        }
    }
}

impl Display for Type {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result {
        let ident: String = if self.ident_kind != TypeIdentKind::Builtin {
            self.ident.clone()
        } else if self.ident == TypeKind::Int.to_string() || self.ident == TypeKind::Uint.to_string() || self.ident == TypeKind::Float.to_string() {
            self.ident.clone()
        } else {
            "".to_string()
        };

        if ident.is_empty() {
            return write!(f, "{}", self._type_format());
        }

        if !self.args.is_empty() {
            let args_str: String = self.args.iter().map(|arg| arg._type_format()).collect::<Vec<_>>().join(",");
            return write!(f, "{}<{}>({})", ident, args_str, self._type_format());
        }

        return write!(f, "{}({})", ident, self._type_format());
    }
}

impl Type {
    pub fn new(mut kind: TypeKind) -> Self {
        kind = Type::cross_kind_trans(&kind);

        let mut t = Self {
            import_as: "".to_string(),
            ident: "".to_string(),
            symbol_id: 0,
            args: Vec::new(),
            ident_kind: TypeIdentKind::Unknown,
            kind: kind.clone(),
            status: ReductionStatus::Done,
            start: 0,
            end: 0,
            in_heap: Self::kind_in_heap(&kind),
            err: false,
        };

        if Self::is_impl_builtin_type(&kind) {
            t.ident = kind.to_string();
            t.ident_kind = TypeIdentKind::Builtin;
        }

        return t;
    }

    pub fn unknown() -> Self {
        Self {
            import_as: "".to_string(),
            ident: "".to_string(),
            symbol_id: 0,
            args: Vec::new(),
            ident_kind: TypeIdentKind::Unknown,
            kind: TypeKind::Unknown,
            status: ReductionStatus::Undo,
            start: 0,
            end: 0,
            in_heap: false,
            err: false,
        }
    }

    pub fn undo_new(kind: TypeKind) -> Self {
        Self {
            import_as: "".to_string(),
            ident: "".to_string(),
            symbol_id: 0,
            args: Vec::new(),
            ident_kind: TypeIdentKind::Unknown,
            kind: kind.clone(),
            status: ReductionStatus::Undo,
            start: 0,
            end: 0,
            in_heap: Self::kind_in_heap(&kind),
            err: false,
        }
    }

    pub fn cross_kind_trans(kind: &TypeKind) -> TypeKind {
        match kind {
            TypeKind::Float => TypeKind::Float64,
            TypeKind::Int => TypeKind::Int64,
            TypeKind::Uint => TypeKind::Uint64,
            _ => kind.clone(),
        }
    }

    pub fn ident_is_generics_param(&self) -> bool {
        if self.kind != TypeKind::Ident {
            return false;
        }

        return self.ident_kind == TypeIdentKind::GenericsParam;
    }

    pub fn is_ident(&self) -> bool {
        if self.kind != TypeKind::Ident {
            return false;
        }

        return self.ident_kind == TypeIdentKind::Def
            || self.ident_kind == TypeIdentKind::Interface
            || self.ident_kind == TypeIdentKind::Enum
            || self.ident_kind == TypeIdentKind::TaggedUnion
            || self.ident_kind == TypeIdentKind::Unknown;
    }

    pub fn error() -> Self {
        let mut result = Self::default();
        result.err = true;
        result.status = ReductionStatus::Done;
        result
    }

    pub fn ident_new(ident: String, ident_kind: TypeIdentKind) -> Self {
        let mut t = Self::undo_new(TypeKind::Ident);
        t.ident = ident;
        t.ident_kind = ident_kind;
        return t;
    }

    fn _type_format(&self) -> String {
        match &self.kind {
            TypeKind::Vec(element_type) => {
                format!("[{}]", element_type)
            }
            TypeKind::Chan(element_type) => {
                format!("chan<{}>", element_type)
            }
            TypeKind::Arr(_, length, element_type) => {
                format!("[{};{}]", element_type, length)
            }
            TypeKind::Map(key_type, value_type) => {
                format!("map<{},{}>", key_type, value_type)
            }
            TypeKind::Set(element_type) => {
                format!("set<{}>", element_type)
            }
            TypeKind::Tuple(_elements, _) => {
                // 简化版本，可以根据需要实现完整版本
                "tup<...>".to_string()
            }
            TypeKind::Fn(type_fn) => {
                format!("fn(...):{}{}", type_fn.return_type, if type_fn.errable { "!" } else { "" })
            }
            TypeKind::Ptr(value_type) => {
                format!("ptr<{}>", value_type)
            }
            TypeKind::Rawptr(value_type) => {
                format!("rawptr<{}>", value_type)
            }
            TypeKind::Union(any, _, _) if *any => "any".to_string(),
            TypeKind::Interface(..) => "interface".to_string(),
            _ => self.kind.to_string(),
        }
    }

    pub fn kind_in_heap(kind: &TypeKind) -> bool {
        matches!(
            kind,
            TypeKind::Union(..)
                | TypeKind::TaggedUnion(..)
                | TypeKind::String
                | TypeKind::Vec(..)
                | TypeKind::Map(..)
                | TypeKind::Set(..)
                | TypeKind::Tuple(..)
                | TypeKind::Fn(..)
                | TypeKind::CoroutineT
                | TypeKind::Chan(..)
                | TypeKind::Interface(..)
        )
    }

    pub fn must_assign_value(kind: &TypeKind) -> bool {
        if matches!(kind, TypeKind::Fn(..) | TypeKind::Ptr(..) | TypeKind::Interface(..)) {
            return true;
        }

        if let TypeKind::Union(_, nullable, _) = kind {
            if !nullable {
                return true;
            }
        }

        return false;
    }

    pub fn is_integer(kind: &TypeKind) -> bool {
        matches!(
            kind,
            TypeKind::Int
                | TypeKind::Uint
                | TypeKind::Int8
                | TypeKind::Uint8
                | TypeKind::Int16
                | TypeKind::Uint16
                | TypeKind::Int32
                | TypeKind::Uint32
                | TypeKind::Int64
                | TypeKind::Uint64
        )
    }

    pub fn is_float(kind: &TypeKind) -> bool {
        matches!(kind, TypeKind::Float32 | TypeKind::Float64 | TypeKind::Float)
    }

    pub fn is_list_u8(kind: &TypeKind) -> bool {
        if let TypeKind::Vec(element_type) = kind {
            matches!(element_type.kind, TypeKind::Uint8)
        } else {
            false
        }
    }

    pub fn can_type_casting(kind: &TypeKind) -> bool {
        Self::is_number(kind) || matches!(kind, TypeKind::Bool)
    }

    pub fn is_scala_type(kind: &TypeKind) -> bool {
        Self::is_number(kind) || matches!(kind, TypeKind::Bool)
    }

    pub fn is_number(kind: &TypeKind) -> bool {
        Self::is_integer(kind) || Self::is_float(kind)
    }

    pub fn is_any(kind: &TypeKind) -> bool {
        let TypeKind::Union(any, _, _) = kind else {
            return false;
        };

        return *any;
    }

    pub fn is_origin_type(kind: &TypeKind) -> bool {
        Self::is_number(kind)
            || matches!(
                kind,
                TypeKind::Anyptr | TypeKind::Void | TypeKind::Null | TypeKind::Bool | TypeKind::String | TypeKind::FnT | TypeKind::AllT
            )
    }

    pub fn is_map_key_type(kind: &TypeKind) -> bool {
        Self::is_number(kind)
            || matches!(
                kind,
                TypeKind::Bool
                    | TypeKind::String
                    | TypeKind::Ptr(..)
                    | TypeKind::Rawptr(..)
                    | TypeKind::Anyptr
                    | TypeKind::Chan(..)
                    | TypeKind::Struct(..)
                    | TypeKind::Arr(..)
            )
    }

    pub fn is_complex_type(kind: &TypeKind) -> bool {
        matches!(
            kind,
            TypeKind::Struct(..)
                | TypeKind::Map(..)
                | TypeKind::Vec(..)
                | TypeKind::Chan(..)
                | TypeKind::Arr(..)
                | TypeKind::Tuple(..)
                | TypeKind::Set(..)
                | TypeKind::Fn(..)
                | TypeKind::Ptr(..)
                | TypeKind::Rawptr(..)
        )
    }

    pub fn type_struct_sizeof(kind: &TypeKind) -> u64 {
        let TypeKind::Struct(_, align, properties) = kind else { unreachable!() };
        // 如果 align 为 0,说明结构体没有元素或嵌套结构体也没有元素
        if *align == 0 {
            return 0;
        }

        let mut size: u64 = 0;

        // 遍历所有属性
        for prop in properties {
            let element_size = Self::sizeof(&prop.type_.kind);
            let element_align = Self::alignof(&prop.type_.kind) as u64;

            // 按照元素的对齐要求对当前偏移量进行对齐
            size = align_up(size, element_align);
            size += element_size;
        }

        // 最后按照结构体整体的对齐要求进行对齐
        size = align_up(size, *align as u64);

        size
    }

    pub fn sizeof(kind: &TypeKind) -> u64 {
        match &kind {
            TypeKind::Struct(..) => Self::type_struct_sizeof(kind),
            TypeKind::Arr(_, len, element_type) => len * Self::sizeof(&element_type.kind),
            _ => kind.sizeof(),
        }
    }

    pub fn alignof(kind: &TypeKind) -> u8 {
        match &kind {
            TypeKind::Struct(_, align, _) => *align as u8,
            TypeKind::Arr(_, _, element_type) => Self::alignof(&element_type.kind),
            _ => kind.sizeof() as u8,
        }
    }

    pub fn is_stack_impl(&self) -> bool {
        Self::is_number(&self.kind)
            || matches!(
                self.kind,
                TypeKind::Anyptr | TypeKind::Bool | TypeKind::Struct(..) | TypeKind::Arr(..) | TypeKind::Enum(..)
            )
    }

    pub fn is_impl_builtin_type(kind: &TypeKind) -> bool {
        Self::is_number(kind)
            || matches!(
                kind,
                TypeKind::Bool | TypeKind::String | TypeKind::Map(..) | TypeKind::Set(..) | TypeKind::Vec(..) | TypeKind::Chan(..) | TypeKind::CoroutineT
            )
    }

    pub fn is_heap_impl(&self) -> bool {
        matches!(
            self.kind,
            TypeKind::String | TypeKind::Map(..) | TypeKind::Set(..) | TypeKind::Vec(..) | TypeKind::Chan(..) | TypeKind::CoroutineT
        )
    }

    pub fn integer_t_new() -> Type {
        let mut t = Type::new(TypeKind::Ident);
        t.kind = Self::cross_kind_trans(&TypeKind::Int);
        t.ident = TypeKind::IntegerT.to_string();
        t.ident_kind = TypeIdentKind::Builtin;
        return t;
    }

    pub fn ptr_of(t: Type) -> Type {
        assert_eq!(t.status, ReductionStatus::Done);
        let ptr_kind = TypeKind::Ptr(Box::new(t.clone()));
        let mut ptr_type = Type::new(ptr_kind);
        ptr_type.start = t.start;
        ptr_type.end = t.end;
        ptr_type.in_heap = false;
        return ptr_type;
    }

    pub fn rawptr_of(t: Type) -> Type {
        assert_eq!(t.status, ReductionStatus::Done);

        let ptr_kind = TypeKind::Rawptr(Box::new(t.clone()));

        let mut ptr_type = Type::new(ptr_kind);
        ptr_type.start = t.start;
        ptr_type.end = t.end;
        ptr_type.in_heap = false;
        return ptr_type;
    }

    /**
     * 计算类型 hash 值
     */
    pub fn hash(&self) -> u64 {
        match &self.kind {
            TypeKind::Vec(element_type) => {
                let element_hash = element_type.hash();
                let mut hasher = DefaultHasher::new();
                format!("{}.{}", self.kind.to_string(), element_hash).hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Chan(element_type) => {
                let element_hash = element_type.hash();
                let mut hasher = DefaultHasher::new();
                format!("{}.{}", self.kind.to_string(), element_hash).hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Arr(_, length, element_type) => {
                let element_hash = element_type.hash();
                let mut hasher = DefaultHasher::new();
                format!("{}.{length}_{}", self.kind.to_string(), element_hash).hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Map(key_type, value_type) => {
                let key_hash = key_type.hash();
                let value_hash = value_type.hash();
                let mut hasher = DefaultHasher::new();
                format!("{}.{key_hash}_{value_hash}", self.kind.to_string()).hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Set(element_type) => {
                let element_hash = element_type.hash();
                let mut hasher = DefaultHasher::new();
                format!("{}.{}", self.kind.to_string(), element_hash).hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Tuple(elements, _) => {
                let mut hasher = DefaultHasher::new();
                let mut str = self.kind.to_string();
                for element in elements {
                    str = format!("{}.{}", str, element.hash());
                }
                str.hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Fn(type_fn) => {
                let mut hasher = DefaultHasher::new();
                let mut str = self.kind.to_string();
                str = format!("{}.{}", str, type_fn.return_type.hash());
                for param_type in &type_fn.param_types {
                    str = format!("{}_{}", str, param_type.hash());
                }
                str.hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Ptr(value_type) => {
                let value_hash = value_type.hash();
                let mut hasher = DefaultHasher::new();
                format!("{}.{}", self.kind.to_string(), value_hash).hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Rawptr(value_type) => {
                let value_hash = value_type.hash();
                let mut hasher = DefaultHasher::new();
                format!("{}.{}", self.kind.to_string(), value_hash).hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Union(_any, _, elements) => {
                let mut hasher = DefaultHasher::new();
                let mut str = self.kind.to_string();
                for element in elements {
                    str = format!("{}.{}", str, element.hash());
                }
                str.hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Interface(_elements) => {
                let mut hasher = DefaultHasher::new();
                let mut str = self.kind.to_string();

                if self.ident != "" {
                    str = format!("{}.{}", str, self.ident);
                }

                str.hash(&mut hasher);
                hasher.finish()
            }
            TypeKind::Struct(_ident, _, properties) => {
                let mut hasher = DefaultHasher::new();
                let mut str = self.kind.to_string();
                for prop in properties {
                    str = format!("{}_{}_{}", str, prop.name, prop.type_.hash());
                }
                str.hash(&mut hasher);
                hasher.finish()
            }
            _ => {
                let mut hasher = DefaultHasher::new();
                self.kind.to_string().hash(&mut hasher);
                hasher.finish()
            }
        }
    }
}

// type struct property don't concern the value of the property
#[derive(Debug, Clone)]
pub struct TypeStructProperty {
    pub type_: Type,
    pub name: String,
    pub value: Option<Box<Expr>>,
    pub start: usize,
    pub end: usize,
}

// type enum property
#[derive(Debug, Clone)]
pub struct TypeEnumProperty {
    pub name: String,
    pub value_expr: Option<Box<Expr>>,
    pub value: Option<String>,
}

// tagged union element
#[derive(Debug, Clone)]
pub struct TaggedUnionElement {
    pub tag: String,
    pub type_: Type,
}

#[derive(Debug, Clone)]
pub struct TypeFn {
    pub name: String,
    pub return_type: Type,
    pub param_types: Vec<Type>,
    pub errable: bool,
    pub rest: bool,
    pub tpl: bool,
}

// #[derive(Debug, Clone)]
// pub struct TypeAlias {
//     pub import_as: Option<String>,
//     pub ident: String,
//     pub symbol_id: NodeId,
//     pub args: Option<Vec<Type>>,
// }

// impl TypeAlias {
//     pub fn default() -> Self {
//         Self {
//             import_as: None,
//             ident: String::new(),
//             symbol_id: 0,
//             args: None,
//         }
//     }
// }

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ReductionStatus {
    Undo = 1,
    Doing = 2,
    Doing2 = 3,
    Done = 4,
}

impl Display for ReductionStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            ReductionStatus::Undo => write!(f, "undo"),
            ReductionStatus::Doing => write!(f, "doing"),
            ReductionStatus::Doing2 => write!(f, "doing2"),
            ReductionStatus::Done => write!(f, "done"),
        }
    }
}

// Self parameter kind for impl methods
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SelfKind {
    Null = 0,    // No self parameter
    SelfT,       // self - value type
    SelfRawptrT, // *self - raw pointer type
    SelfPtrT,    // default for impl fn without explicit self
}

impl Default for SelfKind {
    fn default() -> Self {
        SelfKind::Null
    }
}

#[derive(Debug, Clone, PartialEq)]
pub enum TypeIdentKind {
    Unknown = 0,
    Def,
    Alias,
    GenericsParam,
    Builtin,   // int/float/vec/string...
    Interface, // type.impls 部分专用
    Enum,
    TaggedUnion, // tagged union type
}

#[derive(Debug, Clone, Display)]
#[repr(u8)]
pub enum TypeKind {
    #[strum(serialize = "unknown")]
    Unknown,

    // 基础类型
    #[strum(serialize = "null")]
    Null,
    #[strum(serialize = "bool")]
    Bool,

    #[strum(serialize = "i8")]
    Int8,
    #[strum(serialize = "u8")]
    Uint8,
    #[strum(serialize = "i16")]
    Int16,
    #[strum(serialize = "u16")]
    Uint16,
    #[strum(serialize = "i32")]
    Int32,
    #[strum(serialize = "u32")]
    Uint32,
    #[strum(serialize = "i64")]
    Int64,
    #[strum(serialize = "u64")]
    Uint64,
    #[strum(serialize = "int")]
    Int,
    #[strum(serialize = "uint")]
    Uint,

    #[strum(serialize = "f32")]
    Float32,
    #[strum(serialize = "float")]
    Float,
    #[strum(serialize = "f64")]
    Float64,

    // 复合类型
    #[strum(serialize = "string")]
    String,
    #[strum(serialize = "vec")]
    Vec(Box<Type>), // element type

    #[strum(serialize = "arr")]
    Arr(Box<Expr>, u64, Box<Type>), // (length_expr, length, element_type)

    #[strum(serialize = "map")]
    Map(Box<Type>, Box<Type>), // (key_type, value_type)

    #[strum(serialize = "set")]
    Set(Box<Type>), // element type

    #[strum(serialize = "tup")]
    Tuple(Vec<Type>, u8), // (elements, align)

    #[strum(serialize = "chan")]
    Chan(Box<Type>), // element type

    #[strum(serialize = "coroutine_t")]
    CoroutineT,

    #[strum(serialize = "struct")]
    Struct(String, u8, Vec<TypeStructProperty>), // (ident, align, properties)

    #[strum(serialize = "fn")]
    Fn(Box<TypeFn>),

    // 指针类型
    #[strum(serialize = "ptr")]
    Ptr(Box<Type>), // value type

    #[strum(serialize = "rawptr")]
    Rawptr(Box<Type>), // value type

    #[strum(serialize = "anyptr")]
    Anyptr,

    // 编译时特殊临时类型
    #[strum(serialize = "fn_t")]
    FnT,

    #[strum(serialize = "integer_t")]
    IntegerT,

    #[strum(serialize = "floater_t")]
    FloaterT,

    #[strum(serialize = "all_t")]
    AllT,

    #[strum(serialize = "void")]
    Void,

    #[strum(serialize = "raw_string")]
    RawString,

    #[strum(serialize = "union")]
    Union(bool, bool, Vec<Type>), // (any, nullable, elements)

    #[strum(serialize = "tagged_union")]
    TaggedUnion(String, Vec<TaggedUnionElement>), // (ident, elements)

    #[strum(serialize = "interface")]
    Interface(Vec<Type>), // elements

    #[strum(serialize = "enum")]
    Enum(Box<Type>, Vec<TypeEnumProperty>), // (element_type, properties)

    #[strum(serialize = "ident")]
    Ident,
}

impl TypeKind {
    pub fn is_unknown(&self) -> bool {
        matches!(self, TypeKind::Unknown)
    }

    pub fn is_exist(&self) -> bool {
        !self.is_unknown()
    }

    pub fn sizeof(&self) -> u64 {
        match self {
            TypeKind::Void => 0,
            TypeKind::Bool | TypeKind::Int8 | TypeKind::Uint8 => 1,
            TypeKind::Int16 | TypeKind::Uint16 => 2,
            TypeKind::Int32 | TypeKind::Uint32 | TypeKind::Float32 => 4,
            TypeKind::Int64 | TypeKind::Uint64 | TypeKind::Float64 => 8,
            _ => 8,
        }
    }
}

impl PartialEq for TypeKind {
    fn eq(&self, other: &Self) -> bool {
        std::mem::discriminant(self) == std::mem::discriminant(other)
    }
}

// ast struct

#[derive(Debug, Clone, PartialEq, Display)]
pub enum ExprOp {
    #[strum(to_string = "none")]
    None,
    #[strum(to_string = "+")]
    Add,
    #[strum(to_string = "-")]
    Sub,
    #[strum(to_string = "*")]
    Mul,
    #[strum(to_string = "/")]
    Div,
    #[strum(to_string = "%")]
    Rem,

    #[strum(to_string = "!")]
    Not,
    #[strum(to_string = "-")]
    Neg,
    #[strum(to_string = "~")]
    Bnot,
    #[strum(to_string = "&")]
    La,
    #[strum(to_string = "&?")]
    SafeLa,
    #[strum(to_string = "&!")]
    UnsafeLa,

    #[strum(to_string = "*")]
    Ia,

    #[strum(to_string = "&")]
    And,
    #[strum(to_string = "|")]
    Or,
    #[strum(to_string = "^")]
    Xor,
    #[strum(to_string = "<<")]
    Lshift,
    #[strum(to_string = ">>")]
    Rshift,

    #[strum(to_string = "<")]
    Lt,
    #[strum(to_string = "<=")]
    Le,
    #[strum(to_string = ">")]
    Gt,
    #[strum(to_string = ">=")]
    Ge,
    #[strum(to_string = "==")]
    Ee,
    #[strum(to_string = "!=")]
    Ne,

    #[strum(to_string = "&&")]
    AndAnd,
    #[strum(to_string = "||")]
    OrOr,
}

impl ExprOp {
    pub fn is_integer(&self) -> bool {
        matches!(
            self,
            ExprOp::Rem | ExprOp::Lshift | ExprOp::Rshift | ExprOp::And | ExprOp::Or | ExprOp::Xor | ExprOp::Bnot
        )
    }

    pub fn is_bool(&self) -> bool {
        return matches!(self, ExprOp::AndAnd | ExprOp::OrOr);
    }

    pub fn is_logic(&self) -> bool {
        matches!(
            self,
            ExprOp::AndAnd | ExprOp::OrOr | ExprOp::Lt | ExprOp::Le | ExprOp::Gt | ExprOp::Ge | ExprOp::Ee | ExprOp::Ne
        )
    }

    pub fn is_arithmetic(&self) -> bool {
        matches!(
            self,
            ExprOp::Add | ExprOp::Sub | ExprOp::Mul | ExprOp::Div | ExprOp::Rem | ExprOp::Lshift | ExprOp::Rshift | ExprOp::And | ExprOp::Or | ExprOp::Xor
        )
    }
}

#[derive(Debug, Clone)]
pub enum AstNode {
    None,
    Literal(TypeKind, String),                                         // (kind, value)
    Binary(ExprOp, Box<Expr>, Box<Expr>),                              // (op, left, right)
    Unary(ExprOp, Box<Expr>),                                          // (op, operand)
    Ident(String, NodeId),                                             // (ident, symbol_id)
    As(Type, Option<Box<Expr>>, Box<Expr>),                            // (target_type, src)
    Is(Type, Option<Box<Expr>>, Option<Box<Expr>>, Option<Box<Expr>>), // (target_type, union_tag, src, binding) - src=None means match-is

    // marco
    MacroSizeof(Type),       // (target_type)
    MacroUla(Box<Expr>),     // (src)
    MacroReflectHash(Type),  // (target_type)
    MacroTypeEq(Type, Type), // (left_type, right_type)
    MacroAsync(MacroAsyncExpr),
    MacroCall(String, Vec<MacroArg>), // (ident, args)
    MacroDefault(Type),               // type

    New(Type, Vec<StructNewProperty>, Option<Box<Expr>>), // (type_, properties, scalar expr)

    MapAccess(Type, Type, Box<Expr>, Box<Expr>),         // (key_type, value_type, left, key)
    VecAccess(Type, Box<Expr>, Box<Expr>),               // (element_type, left, index)
    VecSlice(Box<Expr>, Box<Expr>, Box<Expr>),           // left, start, end
    ArrayAccess(Type, Box<Expr>, Box<Expr>),             // (element_type, left, index)
    TupleAccess(Type, Box<Expr>, u64),                   // (element_type, left, index)
    StructSelect(Box<Expr>, String, TypeStructProperty), // (instance, key, property)
    EnvAccess(u8, String, NodeId),                       // (index, unique_ident)

    VecRepeatNew(Box<Expr>, Box<Expr>),                                          // default_element, len
    ArrRepeatNew(Box<Expr>, Box<Expr>),                                          // default_element, len
    VecNew(Vec<Box<Expr>>, Option<Box<Expr>>, Option<Box<Expr>>),                // (elements, len, cap)
    ArrayNew(Vec<Box<Expr>>),                                                    // elements
    MapNew(Vec<MapElement>),                                                     // elements
    SetNew(Vec<Box<Expr>>),                                                      //  elements
    TupleNew(Vec<Box<Expr>>),                                                    // elements
    TupleDestr(Vec<Box<Expr>>),                                                  // elements
    StructNew(String, Type, Vec<StructNewProperty>),                             // (ident, type_, properties)
    TaggedUnionNew(Type, String, Option<TaggedUnionElement>, Option<Box<Expr>>), // (union_type, tagged_name, element, arg)
    TaggedUnionElement(Type, String, Option<TaggedUnionElement>),                // (union_type, tagged_name, element) - is option.some

    // 未推断出具体表达式类型
    EmptyCurlyNew,
    AccessExpr(Box<Expr>, Box<Expr>), // (left, key)
    SelectExpr(Box<Expr>, String),    // (left, key)
    VarDecl(Arc<Mutex<VarDeclExpr>>),

    // Statements
    Fake(Box<Expr>), // (expr)

    Break, // (expr)
    Continue,
    Ret(Box<Expr>),
    Import(ImportStmt),                       // 比较复杂直接保留
    VarTupleDestr(Vec<Box<Expr>>, Box<Expr>), // (elements, right)
    Assign(Box<Expr>, Box<Expr>),             // (left, right)
    Return(Option<Box<Expr>>),                // (expr)
    If(Box<Expr>, AstBody, AstBody),          // (condition, consequent, alternate)
    Throw(Box<Expr>),
    TryCatch(AstBody, Arc<Mutex<VarDeclExpr>>, AstBody), // (try_body, catch_err, catch_body)
    Let(Box<Expr>),                                      // (expr)
    ForIterator(Box<Expr>, Arc<Mutex<VarDeclExpr>>, Option<Arc<Mutex<VarDeclExpr>>>, AstBody), // (iterate, first, second, body)

    ForCond(Box<Expr>, AstBody),                            // (condition, body)
    ForTradition(Box<Stmt>, Box<Expr>, Box<Stmt>, AstBody), // (init, cond, update, body)

    // 既可以作为表达式，也可以作为语句
    Call(AstCall),
    Catch(Box<Expr>, Arc<Mutex<VarDeclExpr>>, AstBody), // (try_expr, catch_err, catch_body)
    Match(Option<Box<Expr>>, Vec<MatchCase>),           // (subject, cases)

    Select(Vec<SelectCase>, bool, i16, i16), // (cases, has_default, send_count, recv_count)

    VarDef(Arc<Mutex<VarDeclExpr>>, Box<Expr>), // (var_decl, right)
    Typedef(Arc<Mutex<TypedefStmt>>),
    FnDef(Arc<Mutex<AstFnDef>>),
    ConstDef(Arc<Mutex<AstConstDef>>),
}

impl AstNode {
    pub fn can_assign(&self) -> bool {
        matches!(
            self,
            AstNode::Ident(..)
                | AstNode::AccessExpr(..)
                | AstNode::SelectExpr(..)
                | AstNode::MapAccess(..)
                | AstNode::VecAccess(..)
                | AstNode::EnvAccess(..)
                | AstNode::StructSelect(..)
        )
    }
}

#[derive(Debug, Clone)]
pub struct Stmt {
    pub start: usize,
    pub end: usize,
    pub node: AstNode,
}

#[derive(Debug, Clone)]
pub struct Expr {
    pub start: usize,
    pub end: usize,
    pub type_: Type,
    pub target_type: Type,
    pub node: AstNode,
}

// default
impl Default for Expr {
    fn default() -> Self {
        Self {
            start: 0,
            end: 0,
            type_: Type::default(),
            target_type: Type::default(),
            node: AstNode::None,
        }
    }
}

impl Expr {
    pub fn ident(start: usize, end: usize, literal: String, symbol_id: NodeId) -> Self {
        Self {
            start,
            end,
            type_: Type::default(),
            target_type: Type::default(),
            node: AstNode::Ident(literal, symbol_id),
        }
    }
}

#[derive(Debug, Clone)]
pub struct VarDeclExpr {
    pub ident: String,
    pub symbol_id: NodeId,   // unique symbol table id
    pub symbol_start: usize, // 符号定义位置
    pub symbol_end: usize,   // 符号定义位置
    pub type_: Type,
    pub be_capture: bool,
    pub heap_ident: Option<String>,
}

#[derive(Debug, Clone)]
pub struct AstConstDef {
    pub ident: String,
    pub type_: Type,
    pub right: Box<Expr>,
    pub processing: bool,
    pub symbol_id: NodeId,
    pub symbol_start: usize,
    pub symbol_end: usize,
}

#[derive(Debug, Clone)]
pub struct AstCall {
    pub return_type: Type,
    pub left: Box<Expr>,
    pub generics_args: Vec<Type>,
    pub args: Vec<Box<Expr>>,
    pub spread: bool,
}

#[derive(Debug, Clone)]
pub struct StructNewProperty {
    pub type_: Type,
    pub key: String,
    pub value: Box<Expr>,
    pub start: usize,
    pub end: usize,
}

#[derive(Debug, Clone)]
pub struct MacroAsyncExpr {
    pub closure_fn: Arc<Mutex<AstFnDef>>,
    pub closure_fn_void: Arc<Mutex<AstFnDef>>,
    pub origin_call: Box<AstCall>,
    pub flag_expr: Option<Box<Expr>>,
    pub return_type: Type,
}

#[derive(Debug, Clone)]
pub enum MacroArg {
    Stmt(Box<Stmt>),
    Expr(Box<Expr>),
    Type(Type),
}

#[derive(Debug, Clone)]
pub struct MapElement {
    pub key: Box<Expr>,
    pub value: Box<Expr>,
}

#[derive(Debug, Clone)]
pub struct TupleDestrExpr {
    pub elements: Vec<Box<Expr>>,
}

// 语句实现
#[derive(Debug, Clone)]
pub struct ImportSelectItem {
    pub ident: String,
    pub alias: Option<String>,
}

#[derive(Debug, Clone)]
pub struct ImportStmt {
    pub file: Option<String>,
    pub ast_package: Option<Vec<String>>,
    pub as_name: String,
    pub is_selective: bool,                          // NEW: true if using {item1, item2} syntax
    pub select_items: Option<Vec<ImportSelectItem>>, // NEW: selective import items
    pub module_type: u8,
    pub module_ident: String, //  基于 full path 计算的 unique ident, 如果是 main.n 则 包含 main
    pub full_path: String,
    pub package_conf: Option<PackageConfig>, // import package 时总是依赖该配置
    pub package_dir: String,
    pub use_links: bool,
    pub start: usize, // 冗余自 stmt, 用于 analyzer_import  能够快速处理
    pub end: usize,
}

impl Default for ImportStmt {
    fn default() -> Self {
        Self {
            file: None,
            ast_package: None,
            as_name: String::new(),
            is_selective: false,
            select_items: None,
            module_type: 0,
            full_path: String::new(),
            package_conf: None,
            package_dir: String::new(),
            use_links: false,
            module_ident: String::new(),
            start: 0,
            end: 0,
        }
    }
}

#[derive(Debug, Clone)]
pub struct GenericsParam {
    pub ident: String,
    pub constraints: (Vec<Type>, bool, bool, bool), // (elements, any, and, or)
}

impl GenericsParam {
    pub fn new(ident: String) -> Self {
        Self {
            ident,
            constraints: (Vec::new(), true, false, false),
        }
    }
}

#[derive(Debug, Clone)]
pub struct TypedefStmt {
    pub ident: String,
    pub params: Vec<GenericsParam>,
    pub type_expr: Type,
    pub is_alias: bool,
    pub is_interface: bool,
    pub is_enum: bool,
    pub is_tagged_union: bool,
    pub impl_interfaces: Vec<Type>,
    pub method_table: HashMap<String, Arc<Mutex<AstFnDef>>>, // key = ident, value = ast_fndef_t

    pub symbol_start: usize,
    pub symbol_end: usize,
    pub symbol_id: NodeId,
}

#[derive(Debug, Clone)]
pub struct MatchCase {
    pub cond_list: Vec<Box<Expr>>,
    pub is_default: bool,
    pub handle_body: AstBody,
    pub start: usize,
    pub end: usize,
}

#[derive(Debug, Clone)]
pub struct SelectCase {
    pub on_call: Option<AstCall>,
    pub recv_var: Option<Arc<Mutex<VarDeclExpr>>>,
    pub is_recv: bool,
    pub is_default: bool,
    pub handle_body: AstBody,
    pub start: usize,
    pub end: usize,
}

#[derive(Debug, Clone)]
pub struct AstBody {
    pub stmts: Vec<Box<Stmt>>,
    pub start: usize,
    pub end: usize,
}
impl Default for AstBody {
    fn default() -> Self {
        Self {
            stmts: Vec::new(),
            start: 0,
            end: 0,
        }
    }
}

#[derive(Debug, Clone)]
pub struct AstFnDef {
    pub symbol_name: String,
    pub symbol_id: NodeId,
    pub return_type: Type,
    pub params: Vec<Arc<Mutex<VarDeclExpr>>>,
    pub rest_param: bool,
    pub is_impl: bool,
    pub self_kind: SelfKind,
    pub body: AstBody,
    pub closure: Option<isize>,
    pub generics_args_table: Option<HashMap<String, Type>>,
    pub generics_args_hash: Option<u64>,
    pub generics_params: Option<Vec<GenericsParam>>,
    pub impl_type: Type,
    pub capture_exprs: Vec<Box<Expr>>,
    pub be_capture_locals: Vec<String>,
    pub type_: Type,
    pub generic_assign: Option<HashMap<String, Type>>,
    pub global_parent: Option<Arc<Mutex<AstFnDef>>>,
    pub local_children: Vec<Arc<Mutex<AstFnDef>>>,
    pub is_closure: bool, // fn 如果引用了外部的 var, 就需要编译成闭包
    pub is_local: bool,
    pub is_tpl: bool,
    pub is_generics: bool,
    pub is_async: bool,
    pub is_private: bool,
    pub is_errable: bool, // 当前函数是否返回错误
    pub is_test: bool,
    pub test_name: String,
    pub ret_target_types: Vec<Type>,
    pub linkid: Option<String>,
    pub fn_name: String, // default empty
    pub rel_path: Option<String>,

    // symbol 符号定义位置
    pub symbol_start: usize,
    pub symbol_end: usize,

    // 整个函数的起始与结束位置
    pub module_index: usize, // belong module index
}

// ast fn def default
impl Default for AstFnDef {
    fn default() -> Self {
        Self {
            symbol_name: "".to_string(),
            symbol_id: 0,
            return_type: Type::new(TypeKind::Void),
            params: Vec::new(),
            rest_param: false,
            is_impl: false,
            self_kind: SelfKind::Null,
            body: AstBody {
                stmts: Vec::new(),
                start: 0,
                end: 0,
            },
            closure: None,
            generics_args_table: None,
            generics_args_hash: None,
            generics_params: None,
            impl_type: Type::default(),
            capture_exprs: Vec::new(),
            be_capture_locals: Vec::new(),
            type_: Type::default(),
            generic_assign: None,
            global_parent: None,
            local_children: Vec::new(),
            is_closure: false,
            is_local: false,
            is_tpl: false,
            linkid: None,
            is_generics: false,
            is_async: false,
            is_private: false,
            is_errable: false,
            is_test: false,
            test_name: "".to_string(),
            ret_target_types: Vec::new(),
            fn_name: "".to_string(),
            rel_path: None,
            symbol_start: 0,
            symbol_end: 0,
            module_index: 0,
        }
    }
}
