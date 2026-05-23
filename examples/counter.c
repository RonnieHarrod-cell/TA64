// ============================================================
//  counter.c — ta-gcc counter demo
//  Tests: multiple functions, accumulator pattern, while loops
// ============================================================

int multiply(int a, int b) {
    int result = 0;
    int i = 0;
    while (i < b) {
        result = result + a;
        ++i;
    }
    return result;
}

int power(int base, int exp) {
    int result = 1;
    int i = 0;
    while (i < exp) {
        result = multiply(result, base);
        ++i;
    }
    return result;
}

int sum_squares(int n) {
    int total = 0;
    int i = 1;
    while (i <= n) {
        total = total + multiply(i, i);
        ++i;
    }
    return total;
}

int main() {
    int a = multiply(6, 7);   // 42
    int b = power(2, 8);      // 256
    int c = sum_squares(5);   // 1+4+9+16+25 = 55
    return a;
}
