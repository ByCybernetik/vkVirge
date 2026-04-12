#include <iostream>
#include <locale>

using namespace std;

int multiply(int a, int b) { return a * b; }
int subtract(int a, int b) { return a - b; }


int main(){


int (*op)(int, int) = multiply;
op = subtract;
cout << op(10, 3) << endl;
    return 0;

}

