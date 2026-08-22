// x mode basic syntax coverage, everything here compiles without gc/coroutine/runtime init
import "shape.x"

type point_t = struct {
    int x
    int y
}

type myint = int

int g_counter = 100

fn point_t.sum(*self):int {
    return self.x + self.y
}

fn add(int a, int b):int {
    return a + b
}

fn fib(int n):int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn swap(int a, int b):(int, int) {
    return (b, a)
}

fn bump(ptr<int> v) {
    *v += 10
}

fn classify(int n):int {
    return match {
        n < 0 -> -1
        n == 0 -> 0
        _ -> 1
    }
}

#linkid abs
fn c_abs(i32 v):i32

fn main() {
    // primitive types and literals
    int a = 17
    i8 b = 1
    u64 c = 2
    f64 d = 1.5
    bool e = true
    string s = 'nature'
    var inferred = 42
    println(a, b, c, d, e, s, inferred)

    // arithmetic, comparison, bitwise, logical
    println(a + 5, a - 5, a * 2, a / 5, a % 5)
    println(a > 5, a == 17, a != 5)
    println(a & 5, a | 5, a ^ 5, a << 2, a >> 2, -a, ~a)
    println(true && false, true || false, !false)

    // compound assign
    a += 3
    a *= 2
    println(a)

    // control flow
    if a > 100 {
        println('big')
    } else if a > 10 {
        println('mid')
    } else {
        println('small')
    }

    int sum = 0
    for int i = 1; i <= 10; i += 1 {
        if i == 3 {
            continue
        }
        if i == 9 {
            break
        }
        sum += i
    }
    println(sum)

    int k = 0
    for k < 5 {
        k += 1
    }
    println(k)

    // fn call, recursion, multi return
    println(add(1, 2), fib(15))
    var (x, y) = swap(3, 9)
    println(x, y)

    // struct literal, field access, impl method
    var p = point_t{x: 3, y: 4}
    p.y = 10
    println(p.x, p.y, p.sum())

    // fixed array
    [int;5] arr = [1, 2, 3, 4, 5]
    arr[2] = 30
    int arr_sum = 0
    for int i = 0; i < 5; i += 1 {
        arr_sum += arr[i]
    }
    println(arr[0], arr[2], arr_sum)

    // pointer
    int n = 1
    bump(&n)
    println(n)

    // type alias, global var, const, casting
    const LIMIT = 5
    myint m = 3
    g_counter += 1
    int wide = 300
    i8 narrowed = wide as i8
    f64 widened = 7 as f64
    println(LIMIT, m, g_counter, narrowed, widened)

    // tuple
    var t = (1, true)
    println(t[0], t[1])

    // match
    println(classify(-5), classify(0), classify(9))

    // c ffi through a local #linkid declaration
    println(c_abs(-42))

    // cross module call into another .x module
    println(shape.area(3, 4))
}
