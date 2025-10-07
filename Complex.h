#include <stdio.h>
#include <math.h>

typedef struct {
    double real;
    double imag;
} Complex;

// Função de exemplo para inicializar um número complexo
Complex make_complex(double real, double imag) {
    Complex c;
    c.real = real;
    c.imag = imag;
    return c;
}

// Função de exemplo para multiplicar números complexos
Complex multiply(Complex a, Complex b) {
    Complex result;
    result.real = a.real * b.real - a.imag * b.imag;
    result.imag = a.real * b.imag + a.imag * b.real;
    return result;
}
