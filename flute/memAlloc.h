// 文件功能：声明 FLUTE 数值数组分配、释放、运行错误和排序比较辅助接口。
// 【零基础阅读】接口允许指定数组下标上下界，而不只固定从 0 开始。
// 分配函数可能调整返回指针以适配下标，释放必须用对应 free_* 恢复实际地址。
// 向量是一维，matrix 是二维，3D 是三维；数字类型不同有不同函数版本。
/* --------------------------------------------------------------------------
   Public domain memory allocation and de-allocation routine header file.
   Taken from Appendix B of: 
   Numerical Recipes in C: The Art of Scientific Computing, Second Edition,
   Cambridge University Press, 1992
----------------------------------------------------------------------------*/
#ifndef _MEMALLOC_H_
#define _MEMALLOC_H_
// nl/nh 为向量低/高下标，nrl/nrh 为行范围，ncl/nch 为列范围，ndl/ndh 为第三维范围。
// v/m/t 为需释放的向量/矩阵/张量地址；上下界用来恢复申请地址，不是几何边界。

static float sqrarg;
#define SQR(a) ((sqrarg=(a)) == 0.0 ? 0.0 : sqrarg*sqrarg)

static double dsqrarg;
#define DSQR(a) ((dsqrarg=(a)) == 0.0 ? 0.0 : dsqrarg*dsqrarg)

static double dmaxarg1,dmaxarg2;
#define DMAX(a,b) (dmaxarg1=(a),dmaxarg2=(b),(dmaxarg1) > (dmaxarg2) ?\
(dmaxarg1) : (dmaxarg2))

static double dminarg1,dminarg2;
#define DMIN(a,b) (dminarg1=(a),dminarg2=(b),(dminarg1) < (dminarg2) ?\
(dminarg1) : (dminarg2))

static float maxarg1,maxarg2;
#define FMAX(a,b) (maxarg1=(a),maxarg2=(b),(maxarg1) > (maxarg2) ?\
(maxarg1) : (maxarg2))

static float minarg1,minarg2;
#define FMIN(a,b) (minarg1=(a),minarg2=(b),(minarg1) < (minarg2) ?\
(minarg1) : (minarg2))

static long lmaxarg1,lmaxarg2;
#define LMAX(a,b) (lmaxarg1=(a),lmaxarg2=(b),(lmaxarg1) > (lmaxarg2) ?\
(lmaxarg1) : (lmaxarg2))

static long lminarg1,lminarg2;
#define LMIN(a,b) (lminarg1=(a),lminarg2=(b),(lminarg1) < (lminarg2) ?\
(lminarg1) : (lminarg2))

static int imaxarg1,imaxarg2;
#define IMAX(a,b) (imaxarg1=(a),imaxarg2=(b),(imaxarg1) > (imaxarg2) ?\
(imaxarg1) : (imaxarg2))

static int iminarg1,iminarg2;
#define IMIN(a,b) (iminarg1=(a),iminarg2=(b),(iminarg1) < (iminarg2) ?\
(iminarg1) : (iminarg2))

#define SIGN(a,b) ((b) >= 0.0 ? fflute_abs(a) : -fflute_abs(a))
#define MAX(a,b) ((a)>(b) ? (a) : (b))
#define MIN(a,b) ((a)<(b) ? (a) : (b))

void runtimeError(char error_text[]);
float *vector(long nl, long nh);
int *ivector(long nl, long nh);
unsigned char *cvector(long nl, long nh);
unsigned long *lvector(long nl, long nh);
double *dvector(long nl, long nh);
float **matrix(long nrl, long nrh, long ncl, long nch);
double **dmatrix(long nrl, long nrh, long ncl, long nch);
int **imatrix(long nrl, long nrh, long ncl, long nch);
char **cmatrix(long nrl, long nrh, long ncl, long nch);
unsigned long **lmatrix(long nrl, long nrh, long ncl, long nch);
float **submatrix(float **a, long oldrl, long oldrh, long oldcl, long oldch,
                  long newrl, long newcl);
float **convert_matrix(float *a, long nrl, long nrh, long ncl, long nch);
float ***f3tensor(long nrl, long nrh, long ncl, long nch, long ndl, long ndh);
unsigned long ***lmatrix3D(long nrl, long nrh, long ncl, long nch, long ndl, long ndh);
int ***imatrix3D(int nrl, int nrh, int ncl, int nch, int ndl, int ndh);

void free_vector(float *v, long nl, long nh);
void free_ivector(int *v, long nl, long nh);
void free_cvector(unsigned char *v, long nl, long nh);
void free_lvector(unsigned long *v, long nl, long nh);
void free_dvector(double *v, long nl, long nh);
void free_matrix(float **m, long nrl, long nrh, long ncl, long nch);
void free_dmatrix(double **m, long nrl, long nrh, long ncl, long nch);
void free_imatrix(int **m, long nrl, long nrh, long ncl, long nch);
void free_cmatrix(char **m, long nrl, long nrh, long ncl, long nch);
void free_lmatrix(unsigned long **m, long nrl, long nrh, long ncl, long nch);
void free_submatrix(float **b, long nrl, long nrh, long ncl, long nch);
void free_convert_matrix(float **b, long nrl, long nrh, long ncl, long nch);
void free_f3tensor(float ***t, long nrl, long nrh, long ncl, long nch,
                   long ndl, long ndh);
void free_lmatrix3D(unsigned long ***t, long nrl, long nrh, long ncl, long nch,
                   long ndl, long ndh);
void free_imatrix3D(int ***t, int nrl, int nrh, int ncl, int nch, int ndl, int ndh);
int comp_float(const void *i, const void *j);
int comp_int(const void *i, const void *j);

#endif /* _MEMALLOC_H_ */
