// ============================================================
//  example.c — ta-gcc demonstration
//  Tests: variables, arithmetic, loops, functions, if/while
// ============================================================

int add(int a, int b) {
    return a + b;
}

int factorial(int n) {
    int result = 1;
    int i = 1;
    while (i <= n) {
        result = result * i;
        ++i;
    }
    return result;
}

int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int main() {
    int x = 5;
    int y = 10;
    int sum = add(x, y);

    int fact5 = factorial(5);

    int g = gcd(48, 18);

    return sum;
}
