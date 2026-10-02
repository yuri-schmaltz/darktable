#include <locale.h>
#include <glib.h>
#include "results.h"
int main(void){
  setlocale(LC_ALL,"");
  dt_cli_results_t*r=dt_cli_results_new();
  dt_cli_results_add(r,"in/a.cr3","out/a.tif",DT_CLI_EXIT_OK,NULL,1.25);
  dt_cli_results_add(r,"in/b\"quote\\back.cr3",NULL,DT_CLI_EXIT_JOB_FAILED,"no such file\twith tab\nand newline",0.75);
  dt_cli_results_add(r,"ctl\x01\x02",NULL,DT_CLI_EXIT_LIBRARY,"db locked",0.0);
  dt_cli_results_print_json(r,stdout);
  dt_cli_results_free(r);
  return 0;
}
