#include <time.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "sgx_urts.h"

#include "enclave_u.h"

#include "naive_operators.h"
#include "test.h"
#include "main.h"

#define NAI_SELECTOR_RESULT_FILE "./results/src/nai_selector.txt"
#define NAI_PROJECTOR_RESULT_FILE "./results/src/nai_projector.txt"
#define NAI_JOINER_RESULT_FILE "./results/src/nai_joiner.txt"
#define NAI_AGGREGATOR_RESULT_FILE "./results/src/nai_aggregator.txt"

#define CT_FILE_NAME "./qshield.db"
#define STORE_RESULT_FILE "./results/src/store.txt"

#define SGX_SELECTOR_RESULT_FILE "./results/src/sgx_selector.txt"
#define SGX_PROJECTOR_RESULT_FILE "./results/src/sgx_projector.txt"
#define SGX_JOINER_RESULT_FILE "./results/src/sgx_joiner.txt"
#define SGX_AGGREGATOR_RESULT_FILE "./results/src/sgx_aggregator.txt"

#define MAX_BUF_LEN 100

#if HAVE_CONFIG_H
#include <config.h>
#endif

void o_print_str(const char* str){
  printf("%s", str);
}


char* gen_rand_str(int length)
{
	int flag, i;
	char* string;
	srand((unsigned) time(NULL ));
	if ((string = (char*) malloc(length)) == NULL )
	{
		printf("Malloc failed!flag:14\n");
		return NULL ;
	}

	for (i = 0; i < length - 1; i++)
	{
		flag = rand() % 3;
		switch (flag)
		{
			case 0:
				string[i] = 'A' + rand() % 26;
				break;
			case 1:
				string[i] = 'a' + rand() % 26;
				break;
			case 2:
				string[i] = '0' + rand() % 10;
				break;
			default:
				string[i] = 'x';
				break;
		}
	}
	string[length - 1] = '\0';
	return string;
}

sgx_enclave_id_t global_eid = 0;
int initialize_enclave(void)
{
    sgx_launch_token_t token = {0};
    sgx_status_t ret = SGX_ERROR_UNEXPECTED;
    int updated = 0;

    /* Call sgx_create_enclave to initialize an enclave instance */
    /* Debug Support: set 2nd parameter to 1 */
    ret = sgx_create_enclave(ENCLAVE_FILENAME, SGX_DEBUG_FLAG, &token, &updated, &global_eid, NULL);
    if (ret != SGX_SUCCESS) {
      printf("main: error %#x,failed to create enclave\n",ret );
        return -1;
    }

    return 0;
}


int main(int argc, char** argv)
{

  char param[10240];
  FILE *fp = stdin;
  int docs_n;
  if(argc > 2){
    fp = fopen(argv[1], "r");
    if(!fp){
      printf("Error opening %s\n", argv[1]);
    }
    docs_n = atoi(argv[2]);
  }else{
    printf("Usage: .\\QShield <param_path> <docs_num>\n");
    sgx_destroy_enclave(global_eid);
    exit(-1);
  }
  size_t count = fread(param, 1, 10240, fp);
  if(!count){
    printf("Input error\n");
    sgx_destroy_enclave(global_eid);
    return -1;
  }
  fclose(fp);

  struct rlimit rlim, rlim_new;
  if (getrlimit(RLIMIT_STACK, &rlim)==0) {
    rlim_new.rlim_cur = rlim_new.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_STACK, &rlim_new)!=0) {
      rlim_new.rlim_cur = rlim_new.rlim_max = rlim.rlim_max;
      (void) setrlimit(RLIMIT_STACK, &rlim_new);
    }
  }

  /***********************************************************************
   *                data and predicates construction                     *
   ***********************************************************************/
   void *msg = (void *)malloc(sizeof(coll_db_t));
   {
     (((coll_db_t *)msg))->coll_num = 1;
     ((coll_db_t *)msg)->colls[0].docs_num = docs_n;
     memcpy((((coll_db_t *)msg))->colls[0].coll_id, "C1", 3);
     for(int i = 0; i < docs_n; i++){
       (((coll_db_t *)msg))->colls[0].docs[i].attrs_num = 2;
       memcpy((((coll_db_t *)msg))->colls[0].docs[i].attrs[0].name, "a1", 3);
       memcpy((((coll_db_t *)msg))->colls[0].docs[i].attrs[0].value, "11", 3);
       memcpy((((coll_db_t *)msg))->colls[0].docs[i].attrs[1].name, "a3", 3);
       memcpy((((coll_db_t *)msg))->colls[0].docs[i].attrs[1].value, "1", 2);
     }

   }

   // assembling predicates for operators
   // select: a1 < 100 [C1]
   pred_t *s_pred = (pred_t *)malloc(sizeof(pred_t));
   {
     s_pred->attrs_num = 1;
     strncpy(s_pred->attr_names[0], "a1", 3);
     strncpy(s_pred->attr_values[0], "20", 3);
     strncpy(s_pred->op, "<",2);
     s_pred->tp = SELECTOR;
     s_pred->colls_num = 1;
     strncpy(s_pred->colls[0], "C1", 3);
   }

   //project: a1 [C1]
   pred_t *p_pred_c1 = (pred_t *)malloc(sizeof(pred_t));
   {
     p_pred_c1->attrs_num = 1;
     strncpy(p_pred_c1->attr_names[0], "a1", 3);
     p_pred_c1->tp = PROJECTOR;
     p_pred_c1->colls_num = 1;
     strncpy(p_pred_c1->colls[0], "C1", 3);
   }

   //project: a3, a5 [C1]
   pred_t *p_pred_c2 = (pred_t *)malloc(sizeof(pred_t));
   {
     p_pred_c2->attrs_num = 2;
     strncpy(p_pred_c2->attr_names[0], "a3", 3);
     strncpy(p_pred_c2->attr_names[1], "a5", 3);
     p_pred_c2->tp = PROJECTOR;
     p_pred_c2->colls_num = 1;
     strncpy(p_pred_c2->colls[0], "C1", 3);
   }

   //join: a3 [C1] = a3 [C1]
   pred_t *j_pred = (pred_t *)malloc(sizeof(pred_t));
   {
     j_pred->attrs_num = 1;
     strncpy(j_pred->attr_names[0], "a3", 3);
     j_pred->tp = JOINER;
     j_pred->colls_num = 2;
     strncpy(j_pred->colls[0], "C1", 3);
     strncpy(j_pred->colls[1], "C1", 3);
   }

   //aggregate: a4 [C1] SUM
   pred_t *a_pred = (pred_t *)malloc(sizeof(pred_t));
   {
     a_pred->attrs_num = 1;
     strncpy(a_pred->attr_names[0], "a3", 3);
     a_pred->tp = AGGREGATOR;
     strncpy(a_pred->fun, "SUM", 4);
     a_pred->colls_num = 1;
     strncpy(a_pred->colls[0], "C1", 3);
   }

   // assembling predicates for operators
   // select: a1 < 100 [C1]
   e_pred_t *e_s_pred = (e_pred_t *)malloc(sizeof(e_pred_t));
   {
     e_s_pred->attrs_num = 1;
     strncpy(e_s_pred->attr_names[0], "a1", 3);
     strncpy(e_s_pred->attr_values[0], "20", 3);
     strncpy(e_s_pred->op, "<",2);
     e_s_pred->tp = SELECTOR;
     e_s_pred->colls_num = 1;
     strncpy(e_s_pred->colls[0], "C1", 3);
   }

   //project: a1 [C1]
   e_pred_t *e_p_pred_c1 = (e_pred_t *)malloc(sizeof(e_pred_t));
   {
     e_p_pred_c1->attrs_num = 1;
     strncpy(e_p_pred_c1->attr_names[0], "a1", 3);
     e_p_pred_c1->tp = PROJECTOR;
     e_p_pred_c1->colls_num = 1;
     strncpy(e_p_pred_c1->colls[0], "C1", 3);
   }

   //project: a3, a5 [C1]
   e_pred_t *e_p_pred_c2 = (e_pred_t *)malloc(sizeof(e_pred_t));
   {
     e_p_pred_c2->attrs_num = 2;
     strncpy(e_p_pred_c2->attr_names[0], "a3", 3);
     strncpy(e_p_pred_c2->attr_names[1], "a5", 3);
     e_p_pred_c2->tp = PROJECTOR;
     e_p_pred_c2->colls_num = 1;
     strncpy(e_p_pred_c2->colls[0], "C1", 3);
   }

   //join: a3 [C1] = a3 [C1]
   e_pred_t *e_j_pred = (e_pred_t *)malloc(sizeof(e_pred_t));
   {
     e_j_pred->attrs_num = 1;
     strncpy(e_j_pred->attr_names[0], "a3", 3);
     e_j_pred->tp = JOINER;
     e_j_pred->colls_num = 2;
     strncpy(e_j_pred->colls[0], "C1", 3);
     strncpy(e_j_pred->colls[1], "C1", 3);
   }

   //aggregate: a4 [C1] SUM
   e_pred_t *e_a_pred = (e_pred_t *)malloc(sizeof(e_pred_t));
   {
     e_a_pred->attrs_num = 1;
     strncpy(e_a_pred->attr_names[0], "a3", 3);
     e_a_pred->tp = AGGREGATOR;
     strncpy(e_a_pred->fun, "SUM", 4);
     e_a_pred->colls_num = 1;
     strncpy(e_a_pred->colls[0], "C1", 3);
   }

 /***********************************************************************
  *                   enclave initialization code                       *
  ***********************************************************************/

  // /* Initialize the enclave */
  // if(initialize_enclave() < 0){
  //   printf("Enter a character before exit ...\n");
  //   getchar();
  //   return -1;
  // }
  //
  // /* Initialize the memory region for states in enclave */
  // sgx_status_t ret = SGX_SUCCESS;
  // {
  //   e_states_init(global_eid);
  //
  //   e_pairing_init(global_eid, &ret, param, count);
  //   if(SGX_SUCCESS != ret){
  //     printf("Enclave initialize pairing error!\n");
  //     sgx_destroy_enclave(global_eid);
  //     return -1;
  //   }else{
  //     printf("Enclave initialize pairing ok!\n");
  //   }
  //
  //   // e_rsa_ecdsa_init(global_eid, &ret, 32, 4);
  //   // if(SGX_SUCCESS != ret){
  //   //   printf("Enclave initialize rsa and ecdsa (signature) error!\n");
  //   //   switch(ret){
  //   //       case SGX_ERROR_INVALID_PARAMETER:
  //   //         printf("Invalid parameter!\n");
  //   //         break;
  //   //       case SGX_ERROR_OUT_OF_MEMORY:
  //   //         printf("Out of memory!\n");
  //   //         break;
  //   //       case SGX_ERROR_UNEXPECTED:
  //   //         printf("Error unexpected\n");
  //   //         break;
  //   //   }
  //   //   sgx_destroy_enclave(global_eid);
  //   //   return -1;
  //   // }else{
  //   //   printf("Enclave initialize rsa and ecdsa (signature) ok!\n");
  //   // }
  //
  // }

  /***********************************************************************
   *                        enclave enc/dec code                         *
   ***********************************************************************/
  //  size_t msg_size = sizeof(coll_db_t);
  //  size_t ct_size = sizeof(aes_gcm_data_t) + msg_size;
  //  void *ct = (void *)malloc(ct_size);
  //  e_encrypt(global_eid, &ret, (uint8_t *) msg, msg_size, (uint8_t *)ct, ct_size);
  //  if(SGX_SUCCESS != ret){
  //    printf("Enclave encrypt message error!\n");
  //    switch(ret){
  //        case SGX_ERROR_INVALID_PARAMETER:
  //          printf("Invalid parameter!\n");
  //          break;
  //        case SGX_ERROR_OUT_OF_MEMORY:
  //          printf("Out of memory!\n");
  //          break;
  //        case SGX_ERROR_UNEXPECTED:
  //          printf("Error unexpected\n");
  //          break;
  //    }
  //    free(msg);
  //    free(ct);
  //    sgx_destroy_enclave(global_eid);
  //    return -1;
  //  }else{
  //    printf("Enclave encrypt message ok!\n");
  //    free(msg);
  //  }
  //
  // e_state_idx_t *idx;
  // {
  //   void *tk = (void *)malloc(1);
  //   void *tmp = (void *)malloc(sizeof(e_state_idx_t));
  //   e_decrypt(global_eid, &ret, (uint8_t *)tk, 1, (uint8_t *)ct, ct_size, tmp);
  //   if(SGX_SUCCESS != ret){
  //     printf("Enclave decrypt ciphers with token error!\n");
  //     switch(ret){
  //         case SGX_ERROR_INVALID_PARAMETER:
  //           printf("Invalid parameter!\n");
  //           break;
  //         case SGX_ERROR_OUT_OF_MEMORY:
  //           printf("Out of memory!\n");
  //           break;
  //         case SGX_ERROR_UNEXPECTED:
  //           printf("Error unexpected\n");
  //           break;
  //     }
  //     free(tk);
  //     free(ct);
  //     sgx_destroy_enclave(global_eid);
  //     return -1;
  //   }else{
  //     printf("Enclave decrypt ciphers with token ok!\n");
  //     free(tk);
  //     free(ct);
  //   }
  //   idx = (e_state_idx_t *)tmp;
  // }
  // printf("state index: repo id - %d, state id - %s\n", idx->repo_id, idx->s_id);


  /***********************************************************************
   *                    enclave operator test code                       *
   ***********************************************************************/

  // sgx_selector_t(SGX_SELECTOR_RESULT_FILE, docs_n, e_s_pred, idx);
  // sgx_projector_t(SGX_PROJECTOR_RESULT_FILE, docs_n, e_p_pred_c1, idx);
  // sgx_aggregator_t(SGX_AGGREGATOR_RESULT_FILE, docs_n, e_a_pred, idx);
  // sgx_joiner_t(SGX_JOINER_RESULT_FILE, docs_n, e_j_pred, idx, idx);


  /* Destroy the enclave */
  // sgx_destroy_enclave(global_eid);


  /***********************************************************************
   *                   naive operator test code                          *
   ***********************************************************************/

  // void *state_init = (void *)malloc(sizeof(state_idx_t));
  // {
  //   if(states_init((uint8_t*) msg, state_init)){
  //     printf("initialize state successfully\n");
  //   }
  // }

  // nai_selector_t(NAI_SELECTOR_RESULT_FILE, docs_n, s_pred, (state_idx_t *)state_init);
  // nai_projector_t(NAI_PROJECTOR_RESULT_FILE, docs_n, p_pred_c1, (state_idx_t *)state_init);
  // nai_aggregator_t(NAI_AGGREGATOR_RESULT_FILE, docs_n, a_pred, (state_idx_t *)state_init);
  // nai_joiner_t(NAI_JOINER_RESULT_FILE, docs_n, j_pred, (state_idx_t *)state_init, (state_idx_t *)state_init);

  /***********************************************************************
   *                       store data test code                          *
   ***********************************************************************/

  // size_t msg_size = sizeof(coll_db_t);
  // store_t(STORE_RESULT_FILE, (uint8_t *)msg, msg_size, CT_FILE_NAME, docs_n);

  return 0;
}
