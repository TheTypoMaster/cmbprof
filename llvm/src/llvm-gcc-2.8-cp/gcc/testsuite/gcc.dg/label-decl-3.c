/* Test diagnostics for label declarations.  Test with
   -pedantic-errors.  */
/* Origin: Joseph Myers <joseph@codesourcery.com> */
/* { dg-do compile } */
/* { dg-options "-pedantic-errors" } */

typedef int b;

void
f (void)
{
  __label__ a, b, c, d;
  __extension__ (void)&&d; /* { dg-error "error: label 'd' used but not defined" } */
  /* { dg-error "error: ISO C forbids label declarations" "label decls" { target *-*-* } 13 } */
  goto c; /* { dg-error "error: label 'c' used but not defined" } */
 a: (void)0;
 b: (void)0;
}
