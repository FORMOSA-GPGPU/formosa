#ifndef _GAUSSIANELIM
#define _GAUSSIANELIM

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "clutils.h"

// All OpenCL headers. Use the Khronos headers + ICD loader on every platform
// (including macOS): this project targets the FORMOSA pocl ICD, not Apple's
// system OpenCL.framework, whose deprecated headers do not compile here.
#include <CL/opencl.h>

float *OpenClGaussianElimination(cl_context context, int timing);

void printUsage();
int parseCommandline(int argc, char *argv[], std::string *filename, int *q,
                     int *t, int *p, int *d, int *s);

void InitPerRun(int size, float *m);
void ForwardSub(cl_context context, float *a, float *b, float *m, int size,
                int timing);
void BackSub(float *a, float *b, float *finalVec, int size);
void Fan1(float *m, float *a, int Size, int t);
void Fan2(float *m, float *a, float *b, int Size, int j1, int t);
// void Fan3(float *m, float *b, int Size, int t);
void InitMat(FILE *fp, int size, float *ary, int nrow, int ncol);
void InitAry(FILE *fp, float *ary, int ary_size);
void PrintMat(float *ary, int size, int nrow, int ncolumn);
void PrintAry(float *ary, int ary_size);
float eventTime(cl_event event, cl_command_queue command_queue);
#endif
