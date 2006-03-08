/* zutest.h
 * Scott Bronson
 * 6 Mar 2006
 *
 * This is a ground-up rewrite of Asim Jalis's "CuTest" library.
 * It is released under the MIT License.
 */


// If your compiler doesn't provide __func__, compile with -D__func__='"test"'
#define Fail(...) zutest_fail(__FILE__, __LINE__, __func__, __VA_ARGS__)
// If the expression returns false, it is printed in the failure message.
#define Assert(x) do { if(!(x)) { Fail(#x); } } while(0)
// If the expression returns false, the given format string is printed.
#define AssertFmt(x,...) do { if(!(x)) { Fail(__VA_ARGS__); } } while(0)
// On failure the expression is printed followed by the format string.
#define AssertExp(ex,...) AssertFmt(ex,#ex __VA_ARGS__)

#define AssertExpType(x,y,op,type,fmt) \
	AssertExp(x op y," but "#x"=="fmt" and "#y"=="fmt"!",(type)x,(type)y)

// get ready...
// These only work with integer and pointer values!
#define AssertEq(x,y) AssertOp(x,y,==)
#define AssertNe(x,y) AssertOp(x,y,!=)
#define AssertGt(x,y) AssertOp(x,y,>)
#define AssertGe(x,y) AssertOp(x,y,>=)
#define AssertLt(x,y) AssertOp(x,y,<)
#define AssertLe(x,y) AssertOp(x,y,<=)
#define AssertIntEq(x,y) AssertOp(x,y,==)
#define AssertIntNe(x,y) AssertOp(x,y,!=)
#define AssertIntGt(x,y) AssertOp(x,y,>)
#define AssertIntGe(x,y) AssertOp(x,y,>=)
#define AssertIntLt(x,y) AssertOp(x,y,<)
#define AssertIntLe(x,y) AssertOp(x,y,<=)
#define AssertOp(x,y,op) AssertExpType(x,y,op,long,"%ld")
// Same as above but the values are printed in hex rather than decimal.
#define AssertEqHex(x,y) AssertHexOp(x,y,==)
#define AssertNeHex(x,y) AssertHexOp(x,y,!=)
#define AssertGtHex(x,y) AssertHexOp(x,y,>)
#define AssertGeHex(x,y) AssertHexOp(x,y,>=)
#define AssertLtHex(x,y) AssertHexOp(x,y,<)
#define AssertLeHex(x,y) AssertHexOp(x,y,<=)
// Same as above but all 8 digits are printed
// If you give me a 64 bit computer, I will give you 16 digits!
#define AssertHexOp(x,y,op) AssertExpType(x,y,op,long,"0x%lX")
#define AssertPtr(p)  AssertFmt(p != NULL, \
		#p" != NULL but "#p"==0x%08lX!", (unsigned long)p)
#define AssertNull(p) AssertFmt(p == NULL, \
		#p" == NULL but "#p"==0x%08lX!", (unsigned long)p)
#define AssertPtrNull(p) AssertNull(p)
#define AssertPtrEq(x,y) AssertPtrOp(x,y,==)
#define AssertPtrNe(x,y) AssertPtrOp(x,y,!=)
#define AssertPtrGt(x,y) AssertPtrOp(x,y,>)
#define AssertPtrGe(x,y) AssertPtrOp(x,y,>=)
#define AssertPtrLt(x,y) AssertPtrOp(x,y,<)
#define AssertPtrLe(x,y) AssertPtrOp(x,y,<=)
#define AssertPtrOp(x,y,op) AssertExpType(x,y,op,unsigned long,"0x%lX")
// These work with floats and doubles
#define AssertFloatEq(x,y) AssertFloatOp(x,y,==)
#define AssertFloatNe(x,y) AssertFloatOp(x,y,!=)
#define AssertFloatGt(x,y) AssertFloatOp(x,y,>)
#define AssertFloatGe(x,y) AssertFloatOp(x,y,>=)
#define AssertFloatLt(x,y) AssertFloatOp(x,y,<)
#define AssertFloatLe(x,y) AssertFloatOp(x,y,<=)
// Dbl is implemented the same as Float internally.
// We just provide a Dbl and Double names so that the programmer can
// use whatever name she prefers and the macro can exactly equal the type.
#define AssertDblEq(x,y) AssertFloatOp(x,y,==)
#define AssertDblNe(x,y) AssertFloatOp(x,y,!=)
#define AssertDblGt(x,y) AssertFloatOp(x,y,>)
#define AssertDblGe(x,y) AssertFloatOp(x,y,>=)
#define AssertDblLt(x,y) AssertFloatOp(x,y,<)
#define AssertDblLe(x,y) AssertFloatOp(x,y,<=)
#define AssertDoubleEq(x,y) AssertFloatOp(x,y,==)
#define AssertDoubleNe(x,y) AssertFloatOp(x,y,!=)
#define AssertDoubleGt(x,y) AssertFloatOp(x,y,>)
#define AssertDoubleGe(x,y) AssertFloatOp(x,y,>=)
#define AssertDoubleLt(x,y) AssertFloatOp(x,y,<)
#define AssertDoubleLe(x,y) AssertFloatOp(x,y,<=)
#define AssertFloatOp(x,y,op) AssertExpType(x,y,op,double,"%lf")
// These work with strings
#define AssertStrEq(x,y) AssertStrOp(x,y,EQ,==)
#define AssertStrNe(x,y) AssertStrOp(x,y,NE,!=)
#define AssertStrGt(x,y) AssertStrOp(x,y,GT,>)
#define AssertStrGe(x,y) AssertStrOp(x,y,GE,>=)
#define AssertStrLt(x,y) AssertStrOp(x,y,LT,<)
#define AssertStrLe(x,y) AssertStrOp(x,y,LE,<=)
#define AssertStrOp(x,y,opn,op) AssertFmt(strcmp(x,y) op 0, \
	#x" "#opn" "#y" but "#x" is \"%s\" and "#y" is \"%s\"!",x,y)

// if none of those macros above fit your fancy, call fail directly.
void zutest_fail(const char *file, int line, const char *func,
		const char *msg, ...);



typedef void (*zutest_proc)();
typedef zutest_proc *zutest_suite;
typedef zutest_suite *zutest_battery;

void run_zutest_suite(const zutest_suite suite);
void run_zutest_battery(const zutest_battery battery);
void print_zutest_results();

// Call this on the very first line of your application.  If the user
// ran your program with the first arg of "--run-unit-tests", this will
// run the tests and exit.  Otherwise your program will run as normal.
// If you would rather create a dedicated executable, just call
// run_zutest_battery() directly.
void unit_test_check(int argc, char **argv, const zutest_battery battery);

// This runs all the unit tests supplied and then exits.  Use this
// if you want to handle the arguments yourself.
void run_unit_tests(const zutest_battery battery);

