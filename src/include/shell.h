#ifndef SHELL_H
#define SHELL_H
#include "system.h"
#include "string.h"
#include "kb.h"
#include "screen.h"
#include "types.h"
#include "util.h"

void launch_shell(int n);
void login();
void shutdown();
void set_background_color();
void echo();
void help();
void echocolor();
void multiply();
void gcd();
int gcd_couple(int a, int b);
void fibonaci();
void sort();
void box();
void sum();
void dir();
void clearScreen();
void print_array(int arr[], int n);
void fill_array(int arr[], int n);


#endif
