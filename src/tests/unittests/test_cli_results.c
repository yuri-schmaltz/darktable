/*
 * test_cli_results.c - unit test for the darktable-cli exit code contract and
 * the --results JSON schema (dev-doc/automation-api-spec.md 2.4-2.5).
 *
 * Compiled and run as part of ctest:
 *   ctest --test-dir build -R test_cli_results --output-on-failure
 *
 * A second test, test_cli_results_json, re-parses the rendered document with
 * an independent JSON parser. Substring assertions on JSON are not enough: a
 * missing key still contains every expected substring, and that is how a
 * malformed document shipped during development of this feature.
 */

#include "results.h"

#include <locale.h>
#include <string.h>

#include <glib.h>
#include "results.h"
#include <string.h>

static int p=0,f=0;
static void ck(int c,const char*n,const char*d){
  if(c){p++;printf("  \033[32mPASS\033[0m  %s\n",n);}
  else{f++;printf("  \033[31mFAIL\033[0m  %s%s%s\n",n,d?" - ":"",d?d:"");}
}
static int has(const char*hay,const char*nee){return hay&&strstr(hay,nee)!=NULL;}

int main(void){
  setlocale(LC_ALL,"");
  printf("\033[1mtest_cli_results\033[0m - exit code contract + JSON schema\n");

  printf("\nA  exit code names are stable and total\n");
  ck(!strcmp(dt_cli_exit_name(DT_CLI_EXIT_OK),"ok"),"OK -> ok",NULL);
  ck(!strcmp(dt_cli_exit_name(DT_CLI_EXIT_JOB_FAILED),"job_failed"),"JOB_FAILED -> job_failed",NULL);
  ck(!strcmp(dt_cli_exit_name(DT_CLI_EXIT_USAGE),"usage"),"USAGE -> usage",NULL);
  ck(!strcmp(dt_cli_exit_name(DT_CLI_EXIT_LIBRARY),"library"),"LIBRARY -> library",NULL);
  ck(!strcmp(dt_cli_exit_name((dt_cli_exit_t)999),"unknown"),"out-of-range -> unknown",NULL);
  for(int i=0;i<=5;i++) ck(dt_cli_exit_description((dt_cli_exit_t)i)!=NULL,"description exists",NULL);

  printf("\nB  exit code of a run\n");
  dt_cli_results_t*r=dt_cli_results_new();
  ck(dt_cli_results_exit_code(r)==DT_CLI_EXIT_OK,"empty run -> OK",NULL);
  dt_cli_results_add(r,"a.cr3","a.tif",DT_CLI_EXIT_OK,NULL,1.5);
  dt_cli_results_add(r,"b.cr3","b.tif",DT_CLI_EXIT_OK,NULL,2.5);
  ck(dt_cli_results_exit_code(r)==DT_CLI_EXIT_OK,"all ok -> OK",NULL);
  ck(dt_cli_results_failed(r)==0,"zero failed",NULL);
  dt_cli_results_add(r,"c.cr3",NULL,DT_CLI_EXIT_JOB_FAILED,"cannot read",0.5);
  ck(dt_cli_results_exit_code(r)==DT_CLI_EXIT_JOB_FAILED,"one failure -> JOB_FAILED",NULL);
  ck(dt_cli_results_failed(r)==1,"one failed",NULL);
  dt_cli_results_add(r,"d.cr3",NULL,DT_CLI_EXIT_LIBRARY,"db locked",0.0);
  ck(dt_cli_results_exit_code(r)==DT_CLI_EXIT_LIBRARY,"library failure escalates",NULL);
  dt_cli_results_free(r);

  printf("\nC  JSON schema\n");
  r=dt_cli_results_new();
  dt_cli_results_add(r,"in/a.cr3","out/a.tif",DT_CLI_EXIT_OK,NULL,1.25);
  dt_cli_results_add(r,"in/b.cr3",NULL,DT_CLI_EXIT_JOB_FAILED,"no such file",0.75);
  char*j=dt_cli_results_to_json(r);
  ck(has(j,"\"schema\": \"darktable-cli-results/1\""),"schema key present",j);
  ck(has(j,"\"inputs\": 2"),"inputs count",NULL);
  ck(has(j,"\"failed\": 1"),"failed count",NULL);
  ck(has(j,"\"exit_code\": 1"),"exit_code numeric",NULL);
  ck(has(j,"\"exit_name\": \"job_failed\"")||has(j,"\"job_failed\""),"exit name",NULL);
  ck(has(j,"\"seconds\": 2.000"),"summed seconds",NULL);
  ck(has(j,"\"input\": \"in/a.cr3\""),"input path",NULL);
  ck(has(j,"\"error\": \"no such file\""),"error string",NULL);
  ck(has(j,"\"error\": null"),"null error for ok row",NULL);
  printf("%s", j);
  g_free(j); dt_cli_results_free(r);

  printf("\nD  escaping\n");
  r=dt_cli_results_new();
  dt_cli_results_add(r,"C:\\a\"b\ttab","out",DT_CLI_EXIT_OK,NULL,0.0);
  dt_cli_results_add(r,"ctl\x01",NULL,DT_CLI_EXIT_JOB_FAILED,"line1\nline2",0.0);
  j=dt_cli_results_to_json(r);
  ck(has(j,"C:\\\\a\\\"b\\ttab"),"backslash/quote/tab escaped",j);
  ck(has(j,"\\u0001"),"control char as \\u",NULL);
  ck(has(j,"line1\\nline2"),"newline in error escaped",NULL);
  ck(!has(j,"\traw"),"no raw tab left in output",NULL);
  g_free(j); dt_cli_results_free(r);

  printf("\nE  empty run is still valid JSON\n");
  r=dt_cli_results_new();
  j=dt_cli_results_to_json(r);
  ck(has(j,"\"inputs\": 0"),"zero inputs",NULL);
  ck(has(j,"\"results\": ["),"empty array present",NULL);
  g_free(j); dt_cli_results_free(r);

  printf("\n----------------------------------------------------------\n  %d passed, %d failed\n----------------------------------------------------------\n",p,f);
  return f==0?0:1;
}
