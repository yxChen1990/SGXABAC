#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>      /* vsnprintf */
#include <string.h>
#include <stdlib.h>

#include <sgx_tcrypto.h>
#include <sgx_error.h>

#include "pbc/pbc.h"
#include "escheme/e-scheme.h"

#include "enclave.h"
#include "enclave_t.h"

/*
 * Following structs are defined for loaded data e.g., token, cipher, states
 */
#pragma pack(1)
typedef struct _token_t{
  uint8_t c;
  uint32_t w;
  element_t skb;
} token_t;

typedef struct _s_id_t{
  bool is_used;
  char id[STATE_ID_MAX];
} s_id_t;
#pragma pack()

#define STATE_ID_NUM_MAX 20
s_id_t g_ids[STATE_ID_NUM_MAX] = {
  {false, "S1"},
  {false, "S2"},
  {false, "S3"},
  {false, "S4"},
  {false, "S5"},
  {false, "S6"},
  {false, "S7"},
  {false, "S8"},
  {false, "S9"},
  {false, "S10"},
  {false, "S11"},
  {false, "S12"},
  {false, "S13"},
  {false, "S14"},
  {false, "S15"},
  {false, "S16"},
  {false, "S17"},
  {false, "S18"},
  {false, "S19"},
  {false, "S20"},
};

char *eget_g_id(){
  char *tmp = (char *)malloc(STATE_ID_MAX);
  int i = 0;
  for(; i < STATE_ID_NUM_MAX; i++){
    if(g_ids[i].is_used){
      continue;
    }else{
      g_ids[i].is_used = true;
      break;
    }
  }

  if(i == STATE_ID_NUM_MAX){
    return NULL;
  }
  strncpy(tmp, g_ids[i].id, STATE_ID_MAX);
  return tmp;
}

int eget_st_ptr(states_t sts, const char *st_id){
  int i = 0;
  for(; i < sts.states_num; i++){
    if(strcmp(sts.states[i].s_id, st_id)){
      continue;
    }else{
      break;
    }
  }
  if(i == sts.states_num){
    return -1;
  }

  return i;
}

int eget_coll_ptr(coll_db_t colls, const char *coll_id){
  int i = 0;
  for(; i < colls.coll_num; i++){
    if(strcmp(colls.colls[i].coll_id, coll_id)){
      continue;
    }else{
      break;
    }
  }
  if(i == colls.coll_num){
    return -1;
  }

  return i;
}

int eget_attr_ptr(doc_t doc, const char *attr_name){
  int j = 0;
  for(; j < doc.attrs_num; j++){
    if(strcmp(doc.attrs[j].name, attr_name)){
      continue;
    }else{
      break;
    }
  }
  if(j == doc.attrs_num){
    return -1;
  }

  return j;
}

void eitoa(int n, char *str){
  char buf[10] = "";
  int i = 0;
  int len = 0;
  int temp = n < 0 ? -n : n;

  while(temp){
    buf[i++] = (temp % 10) + '0';
    temp = temp / 10;
  }

  len = n < 0 ? ++i : i;
  str[i] = '\0';
  while(1){
    i--;
    if(buf[len-i-1] == 0){
      break;
    }
    str[i] = buf[len-i-1];
  }
  if(i == 0){
    str[i] = '-';
  }
}

bool etag_is_same(sgx_sha256_hash_t* skbi_gti_tag_user,sgx_sha256_hash_t* skbi_gti_tag_ska){
  for (size_t i = 0; i < SKBI_GTI_TAG_LEN; i++) {
  if ((*skbi_gti_tag_user)[i]!=(*skbi_gti_tag_ska)[i]) {
    return false;
   }
  }
  return true;
}

void eprintf(const char *fmt, ...){
  char buf[BUFSIZ] = {'\0'};
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, BUFSIZ, fmt, ap);
  va_end(ap);
  o_print_str(buf);
}

void eprintst(state_t st){
  eprintf("w: %d\n", st.w);
  eprintf("state ID: %s\n", st.s_id);
  eprintf("state generated by function: %s\n", st.f.func_name);
  eprintf("state derived from: ");
  for(int i = 0; i < st.p_states.p_sts_num; i++){
    eprintf("%s ", st.p_states.p_sts[i]);
  }
  eprintf("\n");
  eprintf("state workload (total %d collections):\n\n", st.s_db.coll_num);
  for(int i = 0; i < st.s_db.coll_num; i++){
    eprintf("the %dth collection %s (total %d documents):\n", (i+1), st.s_db.colls[i].coll_id, st.s_db.colls[i].docs_num);
    for(int j = 0; j < st.s_db.colls[i].docs_num; j++){
      eprintf("the %dth document:", (j+1));
      for(int k = 0; k < st.s_db.colls[i].docs[j].attrs_num; k++){
        eprintf("(%s: %s) ", st.s_db.colls[i].docs[j].attrs[k].name, st.s_db.colls[i].docs[j].attrs[k].value);
      }
      eprintf("\n");
    }
    eprintf("\n");
  }
}

/*
 * enclave global variables
 */

pairing_t g_pairing; //for the bilinear map
errmsg_s_ptr g_errmsg;

void *g_rsa_pri_key; //for the enclave's private key
void *g_rsa_pub_key; //for the enclave's public key
sgx_ec256_private_t *g_ecdsa_sign_key; //for the enclave's signing key
sgx_ec256_public_t *g_ecdsa_verify_key; //for the enclave's verifing key

states_t *g_states[REQ_PARALLELISM]; //for all states within the enclave as per user

unsigned char g_policy[32]; //for access policy

e_sk g_e_sk; //for the user's secret share (testing)

void e_states_init(){
  for(int i = 0; i < REQ_PARALLELISM; i++){
    g_states[i] = (states_t *)malloc(sizeof(states_t));
    g_states[i]->states_num = 0;
    g_states[i]->is_occupied = false;

    for(int j = 0; j < STATES_NUM_MAX; j++){
      g_states[i]->states[j].w = 0;
      memset(g_states[i]->states[j].s_id, 0, STATE_ID_MAX);
      memset(g_states[i]->states[j].f.func_name, 0, FUN_NAME_MAX);
      g_states[i]->states[j].p_states.p_sts_num = 0;
      for(int k = 0; k < PRE_STATES_NUM_MAX; k++){
        memset(g_states[i]->states[j].p_states.p_sts[k], 0, STATE_ID_MAX);
      }
      g_states[i]->states[j].s_db.coll_num = 0;
      for(int k = 0; k < STATE_COLLS_NUM_MAX; k++){
        g_states[i]->states[j].s_db.colls[k].docs_num = 0;
        memset(g_states[i]->states[j].s_db.colls[k].coll_id, 0, COLL_ID_MAX);
        for(int m = 0; m < COLL_DOCS_NUM_MAX; m++){
          g_states[i]->states[j].s_db.colls[k].docs[m].attrs_num = 0;
          for(int n = 0; n < DOC_ATTRS_NUM_MAX; n++){
            memset(g_states[i]->states[j].s_db.colls[k].docs[m].attrs[n].name, 0, ATTR_NAME_MAX);
            memset(g_states[i]->states[j].s_db.colls[k].docs[m].attrs[n].value, 0, ATTR_VALUE_MAX);
          }
        }
      }
    }
  }
}

sgx_status_t e_pairing_init(char* param, size_t count){

  if (count < 0){
    eprintf("[Err]: parameter count error\n");
    return SGX_ERROR_UNEXPECTED;
  }

  sgx_status_t ret = SGX_SUCCESS;

  // // sgx_init_errmsg();
  // if(pairing_init_set_buf(g_pairing, param, count)){
  //   eprintf("[Err]: pairing init failed\n");
  //   ret = SGX_ERROR_UNEXPECTED;
  // }
  // // g_errmsg = sgx_get_errmsg();
  // // for(int i = 0; i < g_errmsg->err_num; i++){
  // //   eprintf("[Pbc Debug %d]: %s\n",i, g_errmsg->errs[i].msg);
  // // }
  // // sgx_clear_errmsg();

  ret = ekeygen(&g_e_sk, param, count);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: e-scheme key generation failed\n");
    return ret;
  }

  return ret;

}

sgx_status_t e_rsa_ecdsa_init(int n_byte_size, int e_byte_size){
  sgx_status_t ret = SGX_SUCCESS;

  unsigned char *p_e = (unsigned char *)malloc(e_byte_size);   // unsigned char p_e[4] = {1, 0, 1, 0};
  *p_e = '1';
  *(p_e+1) = '0';
  *(p_e+2) = '1';
  *(p_e+3) = '0';
  unsigned char *p_n = (unsigned char *)malloc(n_byte_size);
  unsigned char *p_d = (unsigned char *)malloc(n_byte_size);
  unsigned char *p_p = (unsigned char *)malloc(n_byte_size);
  unsigned char *p_q = (unsigned char *)malloc(n_byte_size);
  unsigned char *p_dmp1 = (unsigned char *)malloc(n_byte_size);
  unsigned char *p_dmq1 = (unsigned char *)malloc(n_byte_size);
  unsigned char *p_iqmp = (unsigned char *)malloc(n_byte_size);

  sgx_ecc_state_handle_t ecc_handle;
  g_ecdsa_sign_key= (sgx_ec256_private_t *)malloc(32);
  g_ecdsa_verify_key = (sgx_ec256_public_t *)malloc(32);

  ret = sgx_create_rsa_key_pair(n_byte_size, e_byte_size, p_n, p_d, p_e, p_p, p_q, p_dmp1, p_dmq1, p_iqmp);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: creating rsa key pair components failed\n");
    return ret;
  }
  ret = sgx_create_rsa_priv2_key(n_byte_size, e_byte_size, p_e, p_p, p_q, p_dmp1, p_dmq1, p_iqmp, &g_rsa_pri_key);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: creating rsa private key failed\n");
    return ret;
  }
  ret = sgx_create_rsa_pub1_key(n_byte_size, e_byte_size, p_n, p_e, &g_rsa_pub_key);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: creating rsa public key failed\n");
    return ret;
  }

  ret = sgx_ecc256_open_context(&ecc_handle);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: ecc256 context open failed\n");
    return ret;
  }
  ret = sgx_ecc256_create_key_pair(g_ecdsa_sign_key, g_ecdsa_verify_key, ecc_handle);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: creating signature key pair failed\n");
    return ret;
  }
  ret = sgx_ecc256_close_context(ecc_handle);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: ecc256 context close failed\n");
    return ret;
  }

  free(p_e);
  free(p_n);
  free(p_d);
  free(p_p);
  free(p_q);
  free(p_dmp1);
  free(p_dmq1);
  free(p_iqmp);
  return ret;
}

sgx_status_t e_encrypt(uint8_t* msg, size_t msg_size, uint8_t* ct, size_t ct_size){
  sgx_status_t ret = SGX_SUCCESS;

  uint8_t aes_gcm_iv[AES_IV_SIZE] = {0};

  uint32_t len=element_length_in_bytes(g_e_sk.sk);
  unsigned char sk_str[len];
  element_to_bytes(sk_str, g_e_sk.sk);
  uint8_t u_key[16];
  strncpy(u_key,sk_str,16);
  ret = sgx_rijndael128GCM_encrypt(&u_key, &((coll_db_t *)msg)->coll_num, \
                                                  msg_size, \
                                                  ((aes_gcm_data_t *)ct)->payload, \
                                                  &aes_gcm_iv[0], \
                                                  AES_IV_SIZE, \
                                                  NULL, \
                                                  0, \
                                                  &((aes_gcm_data_t *)ct)->payload_tag);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: aes encrypt result failed\n");
    return ret;
  }
  ((aes_gcm_data_t *)ct)->payload_size = msg_size;

  return ret;
}

sgx_status_t e_decrypt(uint8_t* tk, size_t tk_size, uint8_t* ct, size_t ct_size, void* s_idx){
  sgx_status_t ret = SGX_SUCCESS;

  sgx_sha256_hash_t skbi_tag;
  uint32_t skbi_len = element_length_in_bytes(g_e_sk.skb[0]);
  uint8_t skbi_str[skbi_len];
  element_to_bytes(skbi_str, g_e_sk.skb[0]);
  sgx_sha_state_handle_t sha_context;
  ret = sgx_sha256_init(&sha_context);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: sha hash context init failed\n");
    return ret;
  }
  ret = sgx_sha256_update(skbi_str, skbi_len, sha_context);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: sha hash compute over skbi failed\n");
    sgx_sha256_close(sha_context);
    return ret;
  }
  ret = sgx_sha256_get_hash(sha_context, &skbi_tag);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: sha hash retrieve failed\n");
    sgx_sha256_close(sha_context);
    return ret;
  }
  sgx_sha256_close(sha_context);

  //find the gti of skbi
  bool flag = false;
  element_t gti;
  element_init_G1(gti, g_e_sk.ska.pairing);
  for(int i = 0; i < USER_NUM; i++){
    flag=etag_is_same(&skbi_tag, &g_e_sk.ska.comps[i].skbi_tag);
    if(flag){
      element_set(gti, g_e_sk.ska.comps[i].gti);
      break;
    }
  }
  if(!flag){
    eprintf("[Err]: sha user hash tag not found\n");
    return SGX_ERROR_UNEXPECTED;
  }

  //get sk
  element_t tmp1, tmp2;
  element_init_GT(tmp1, g_e_sk.ska.pairing);
  element_init_GT(tmp2, g_e_sk.ska.pairing);
  pairing_apply(tmp1, gti, g_e_sk.skb[0], g_e_sk.ska.pairing);
  element_mul(tmp2, g_e_sk.ska.sk_a, g_e_sk.ska.sk_a);
  element_t sk;
  element_init_GT(sk, g_e_sk.ska.pairing);
  element_div(sk, tmp2, tmp1);

  //decrypt data
  uint32_t len = element_length_in_bytes(sk);
  uint8_t sk_str[len];
  element_to_bytes(sk_str, sk);
  uint8_t u_key[16];
  uint8_t aes_gcm_iv[12] = {0};
  strncpy(u_key, sk_str, 16);
  void *msg0 = (void *)malloc(((aes_gcm_data_t *)ct)->payload_size);
  ret = sgx_rijndael128GCM_decrypt(&u_key, ((aes_gcm_data_t *)ct)->payload, \
                                                  ((aes_gcm_data_t *)ct)->payload_size, \
                                                  (uint8_t *)msg0, \
                                                  &aes_gcm_iv[0], \
                                                  AES_IV_SIZE, \
                                                  NULL, \
                                                  0, \
                                                  &((aes_gcm_data_t *)ct)->payload_tag);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: aes decrypt result failed\n");
    return ret;
  }

  //initialize S0
  state_idx_t idx_tmp;
  {
    idx_tmp.repo_id = 0;
    memset(idx_tmp.s_id, 0, STATE_ID_MAX);

    int i = 0;
    for(; i < REQ_PARALLELISM; i++){
      if(!g_states[i]->is_occupied){
        idx_tmp.repo_id = i;
        break;
      }
    }
    if(i == REQ_PARALLELISM){ //no available memory region for current request.
      ret = SGX_ERROR_OUT_OF_MEMORY;
      return ret;
    }
    g_states[idx_tmp.repo_id]->states_num = 1;
    g_states[idx_tmp.repo_id]->is_occupied = true;
    g_states[idx_tmp.repo_id]->states[0].w = 5; //test
    char id[] = "S0";
    strncpy(g_states[idx_tmp.repo_id]->states[0].s_id, id, sizeof(id));
    strncpy(g_states[idx_tmp.repo_id]->states[0].f.func_name, __FUNCTION__, sizeof(__FUNCTION__));
    memcpy(&g_states[idx_tmp.repo_id]->states[0].s_db.coll_num, &((coll_db_t *)msg0)->coll_num, sizeof(coll_db_t));
    strncpy(idx_tmp.s_id, id, sizeof(id));
    memcpy((uint8_t *)s_idx, &idx_tmp.repo_id, sizeof(state_idx_t));

    eprintf("\n+++++[DEBUG] initialize state: +++++\n");
    eprintst(g_states[idx_tmp.repo_id]->states[0]);
  }

  return ret;
}

sgx_status_t e_projector(struct _pred_t p_pred, struct _state_idx_t s_in, void* s_out){

  if(p_pred.tp != 1 || p_pred.attrs_num == 0 || p_pred.colls_num != 1 \
      || s_in.repo_id >= REQ_PARALLELISM \
        || !g_states[s_in.repo_id]->is_occupied){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  pred_t pred;
  memcpy(&pred.attrs_num, &p_pred.attrs_num, sizeof(pred_t));

  state_idx_t idx_old;
  memcpy(&idx_old.repo_id, &s_in.repo_id, sizeof(state_idx_t));

  state_idx_t idx_new;
  {
    idx_new.repo_id = idx_old.repo_id;
    char *id = eget_g_id();
    if(NULL == id){
      return SGX_ERROR_UNEXPECTED;
    }
    memcpy(idx_new.s_id, id, STATE_ID_MAX);
    free(id);
  }

  uint8_t repo_id = idx_new.repo_id; // get the ID of current repository
  if(g_states[repo_id]->states[0].w == 0){ //the states can not be access any more
    return SGX_ERROR_UNEXPECTED;
  }

  int s_old_ptr = eget_st_ptr(*g_states[repo_id], idx_old.s_id); // get the entry index of the old state
  if(s_old_ptr == -1){
    return SGX_ERROR_INVALID_PARAMETER;
  }
  uint8_t s_new_ptr = g_states[repo_id]->states_num++; //get the entry index of the new state

  {
    for(int i = 0; i < (g_states[repo_id]->states_num - 1); i++){
      g_states[repo_id]->states[i].w--; // all w in relevant states minus 1
    }
    g_states[repo_id]->states[s_new_ptr].w = g_states[repo_id]->states[s_old_ptr].w; //assign the newest w to the new state
    memcpy(g_states[repo_id]->states[s_new_ptr].s_id, idx_new.s_id, STATE_ID_MAX); //assign the new state id
    memcpy(g_states[repo_id]->states[s_new_ptr].f.func_name, __FUNCTION__, sizeof(__FUNCTION__)); //get the function name
    g_states[repo_id]->states[s_new_ptr].p_states.p_sts_num = 1; //assign the number of old states deriving the new state
    memcpy(g_states[repo_id]->states[s_new_ptr].p_states.p_sts[0], idx_old.s_id, STATE_ID_MAX); //assign the old state as its dependency

  }

  {
    int coll_old_ptr = eget_coll_ptr(g_states[repo_id]->states[s_old_ptr].s_db, pred.colls[0]); // get the entry index of the colloection to be processed
    if(coll_old_ptr == -1){
      return SGX_ERROR_INVALID_PARAMETER;
    }

    g_states[repo_id]->states[s_new_ptr].s_db.coll_num = g_states[repo_id]->states[s_old_ptr].s_db.coll_num;
    for(int i = 0; i < g_states[repo_id]->states[s_old_ptr].s_db.coll_num; i++){
      if(i == coll_old_ptr){ //processing this collection
        memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id, g_states[repo_id]->states[s_old_ptr].s_db.colls[i].coll_id, COLL_ID_MAX);
        g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs_num = g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs_num;
        for(int j = 0; j < g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs_num; j++){
          int p = 0;
          for(int k = 0; k < pred.attrs_num; k++){
            for(int q = 0; q < g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[j].attrs_num; q++){
              if(strcmp(g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[j].attrs[q].name, pred.attr_names[k])){ //just delete this item
                continue;
              }else{ //just copy this item
                memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs[j].attrs[p].name[0], &g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[j].attrs[q].name[0], sizeof(attr_t));
                p++;
              }
            }
          }
          g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs[j].attrs_num = p;
        }

      }else{ //copy this collection to the new state
        memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id[0], &g_states[repo_id]->states[s_old_ptr].s_db.colls[i].coll_id[0], sizeof(coll_t));
      }
    }

    eprintf("\n+++++[DEBUG] new generated state: +++++\n");
    eprintst(g_states[repo_id]->states[s_new_ptr]);
  }

  memcpy((uint8_t *)s_out, &idx_new.repo_id, sizeof(state_idx_t));
  return SGX_SUCCESS;
}

sgx_status_t e_selector(struct _pred_t s_pred, struct _state_idx_t s_in, void* s_out){

  if(s_pred.tp != 2 || s_pred.attrs_num != 1 || s_pred.colls_num != 1 \
      || s_in.repo_id >= REQ_PARALLELISM \
        || !g_states[s_in.repo_id]->is_occupied){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  pred_t pred;
  memcpy(&pred.attrs_num, &s_pred.attrs_num, sizeof(pred_t));

  state_idx_t idx_old;
  memcpy(&idx_old.repo_id, &s_in.repo_id, sizeof(state_idx_t));

  state_idx_t idx_new;
  {
    idx_new.repo_id = idx_old.repo_id;
    char *id = eget_g_id();
    if(NULL == id){
      return SGX_ERROR_UNEXPECTED;
    }
    memcpy(idx_new.s_id, id, STATE_ID_MAX);
    free(id);
  }

  uint8_t repo_id = idx_new.repo_id; // get the ID of current repository
  if(g_states[repo_id]->states[0].w == 0){ //the states can not be access any more
    return SGX_ERROR_UNEXPECTED;
  }

  int s_old_ptr = eget_st_ptr(*g_states[repo_id], idx_old.s_id); // get the entry index of the old state
  if(s_old_ptr == -1){
    return SGX_ERROR_INVALID_PARAMETER;
  }
  uint8_t s_new_ptr = g_states[repo_id]->states_num++; //get the entry index of the new state

  {
    for(int i = 0; i < (g_states[repo_id]->states_num - 1); i++){
      g_states[repo_id]->states[i].w--; // all w in relevant states minus 1
    }
    g_states[repo_id]->states[s_new_ptr].w = g_states[repo_id]->states[s_old_ptr].w; //assign the newest w to the new state
    memcpy(g_states[repo_id]->states[s_new_ptr].s_id, idx_new.s_id, STATE_ID_MAX); //assign the new state id
    memcpy(g_states[repo_id]->states[s_new_ptr].f.func_name, __FUNCTION__, sizeof(__FUNCTION__)); //get the function name
    g_states[repo_id]->states[s_new_ptr].p_states.p_sts_num = 1; //assign the number of old states deriving the new state
    memcpy(g_states[repo_id]->states[s_new_ptr].p_states.p_sts[0], idx_old.s_id, STATE_ID_MAX); //assign the old state as its dependency
  }

  {
    int coll_old_ptr = eget_coll_ptr(g_states[repo_id]->states[s_old_ptr].s_db, pred.colls[0]); // get the entry index of the colloection to be processed
    if(coll_old_ptr == -1){
      return SGX_ERROR_INVALID_PARAMETER;
    }

    g_states[repo_id]->states[s_new_ptr].s_db.coll_num = g_states[repo_id]->states[s_old_ptr].s_db.coll_num;
    for(int i = 0; i < g_states[repo_id]->states[s_old_ptr].s_db.coll_num; i++){
      if(i == coll_old_ptr){ //processing this collection

        memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id, g_states[repo_id]->states[s_old_ptr].s_db.colls[i].coll_id, COLL_ID_MAX);

        int p = 0;
        for(int k = 0; k < g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs_num; k++){

          int attr_ptr = eget_attr_ptr(g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[k], pred.attr_names[0]);
          if(attr_ptr == -1){
            continue;
          }

          if(strcmp(pred.op, "<") == 0){
            if(atoi(g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[k].attrs[attr_ptr].value) < atoi(pred.attr_values[0])){
              memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs[p].attrs_num, &g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[k].attrs_num, sizeof(doc_t));
              p++;
            }
          }else if(strcmp(pred.op, "=") == 0){
            if(atoi(g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[k].attrs[attr_ptr].value) == atoi(pred.attr_values[0])){
              memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs[p].attrs_num, &g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[k].attrs_num, sizeof(doc_t));
              p++;
            }
          }else if(strcmp(pred.op, ">") == 0){
            if(atoi(g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[k].attrs[attr_ptr].value) > atoi(pred.attr_values[0])){
              memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs[p].attrs_num, &g_states[repo_id]->states[s_old_ptr].s_db.colls[i].docs[k].attrs_num, sizeof(doc_t));
              p++;
            }
          }

        }

        g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs_num = p;

      }else{ //copy this collection to the new state
        memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id[0], &g_states[repo_id]->states[s_old_ptr].s_db.colls[i].coll_id[0], sizeof(coll_t));
      }
    }

    eprintf("\n+++++[DEBUG] new generated state: +++++\n");
    eprintst(g_states[repo_id]->states[s_new_ptr]);
  }

  memcpy((uint8_t *)s_out, &idx_new.repo_id, sizeof(state_idx_t));
  return SGX_SUCCESS;
}

sgx_status_t e_aggregator(struct _pred_t a_pred, struct _state_idx_t s_in, void* s_out){

  if(a_pred.tp != 3 || a_pred.attrs_num != 1 || a_pred.colls_num != 1\
      || s_in.repo_id >= REQ_PARALLELISM \
        || !g_states[s_in.repo_id]->is_occupied){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  pred_t pred;
  memcpy(&pred.attrs_num, &a_pred.attrs_num, sizeof(pred_t));

  state_idx_t idx_old;
  memcpy(&idx_old.repo_id, &s_in.repo_id, sizeof(state_idx_t));

  state_idx_t idx_new;
  {
    idx_new.repo_id = idx_old.repo_id;
    char *id = eget_g_id();
    if(NULL == id){
      return SGX_ERROR_UNEXPECTED;
    }
    memcpy(idx_new.s_id, id, STATE_ID_MAX);
    free(id);
  }

  uint8_t repo_id = idx_new.repo_id; // get the ID of current repository
  if(g_states[repo_id]->states[0].w == 0){ //the states can not be access any more
    return SGX_ERROR_UNEXPECTED;
  }

  int s_old_ptr = eget_st_ptr(*g_states[repo_id], idx_old.s_id); // get the entry index of the old state
  if(s_old_ptr == -1){
    return SGX_ERROR_INVALID_PARAMETER;
  }
  uint8_t s_new_ptr = g_states[repo_id]->states_num++; //get the entry index of the new state

  {
    for(int i = 0; i < (g_states[repo_id]->states_num - 1); i++){
      g_states[repo_id]->states[i].w--; // all w in relevant states minus 1
    }
    g_states[repo_id]->states[s_new_ptr].w = g_states[repo_id]->states[s_old_ptr].w; //assign the newest w to the new state
    memcpy(g_states[repo_id]->states[s_new_ptr].s_id, idx_new.s_id, STATE_ID_MAX); //assign the new state id
    memcpy(g_states[repo_id]->states[s_new_ptr].f.func_name, __FUNCTION__, sizeof(__FUNCTION__)); //get the function name
    g_states[repo_id]->states[s_new_ptr].p_states.p_sts_num = 1; //assign the number of old states deriving the new state
    memcpy(g_states[repo_id]->states[s_new_ptr].p_states.p_sts[0], idx_old.s_id, STATE_ID_MAX); //assign the old state as its dependency
  }

  {
    int coll_old_ptr = eget_coll_ptr(g_states[repo_id]->states[s_old_ptr].s_db, pred.colls[0]); // get the entry index of the colloection to be processed
    if(coll_old_ptr == -1){
      return SGX_ERROR_INVALID_PARAMETER;
    }

    g_states[repo_id]->states[s_new_ptr].s_db.coll_num = 1;
    g_states[repo_id]->states[s_new_ptr].s_db.colls[0].docs_num = 1;
    memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[0].coll_id, "CAGG", 5);
    g_states[repo_id]->states[s_new_ptr].s_db.colls[0].docs[0].attrs_num = 1;

    int sum_res = 0;
    if(strcmp(pred.fun, "SUM") == 0){
      for(int j = 0; j < g_states[repo_id]->states[s_old_ptr].s_db.colls[coll_old_ptr].docs_num; j++){
        int attr_ptr = eget_attr_ptr(g_states[repo_id]->states[s_old_ptr].s_db.colls[coll_old_ptr].docs[j], pred.attr_names[0]);
        if(attr_ptr == -1){
          continue;
        }
        sum_res += atof(g_states[repo_id]->states[s_old_ptr].s_db.colls[coll_old_ptr].docs[j].attrs[attr_ptr].value);
      }
        memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[0].docs[0].attrs[0].name, "SUM", 4);
        char res[8];
        eitoa(sum_res, res);
        memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[0].docs[0].attrs[0].value, res, sizeof(res));
    }else{
      return SGX_ERROR_UNEXPECTED;
    }

    eprintf("\n+++++[DEBUG] new generated state: +++++\n");
    eprintst(g_states[repo_id]->states[s_new_ptr]);
  }

  memcpy((uint8_t *)s_out, &idx_new.repo_id, sizeof(state_idx_t));
  return SGX_SUCCESS;
}

sgx_status_t e_joiner(struct _pred_t j_pred, struct _state_idx_t s_in_1, struct _state_idx_t s_in_2, void* s_out){

  if(j_pred.tp != 4 || j_pred.attrs_num == 0 || j_pred.colls_num != 2 \
      || s_in_1.repo_id != s_in_2.repo_id || s_in_1.repo_id >= REQ_PARALLELISM \
        || !g_states[s_in_1.repo_id]->is_occupied){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  pred_t pred;
  memcpy(&pred.attrs_num, &j_pred.attrs_num, sizeof(pred_t));

  state_idx_t idx_old_1;
  memcpy(&idx_old_1.repo_id, &s_in_1.repo_id, sizeof(state_idx_t));
  state_idx_t idx_old_2;
  memcpy(&idx_old_2.repo_id, &s_in_2.repo_id, sizeof(state_idx_t));

  state_idx_t idx_new;
  {
    idx_new.repo_id = idx_old_1.repo_id;
    char *id = eget_g_id();
    if(NULL == id){
      return SGX_ERROR_UNEXPECTED;
    }
    memcpy(idx_new.s_id, id, STATE_ID_MAX);
    free(id);
  }

  uint8_t repo_id = idx_new.repo_id; // get the ID of current repository
  if(g_states[repo_id]->states[0].w == 0){ //the states can not be access any more
    return SGX_ERROR_UNEXPECTED;
  }

  int s_old_1_ptr = eget_st_ptr(*g_states[repo_id], idx_old_1.s_id); // get the entry index of the old state
  if(s_old_1_ptr == -1){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  int s_old_2_ptr = eget_st_ptr(*g_states[repo_id], idx_old_2.s_id); // get the entry index of the old state
  if(s_old_2_ptr == -1){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  uint8_t s_new_ptr = g_states[repo_id]->states_num++; //get the entry index of the new state

  {
    for(int i = 0; i < (g_states[repo_id]->states_num - 1); i++){
      g_states[repo_id]->states[i].w--; // all w in relevant states minus 1
    }
    g_states[repo_id]->states[s_new_ptr].w = g_states[repo_id]->states[s_old_1_ptr].w; //assign the newest w to the new state
    memcpy(g_states[repo_id]->states[s_new_ptr].s_id, idx_new.s_id, STATE_ID_MAX); //assign the new state id
    memcpy(g_states[repo_id]->states[s_new_ptr].f.func_name, __FUNCTION__, sizeof(__FUNCTION__)); //get the function name
    g_states[repo_id]->states[s_new_ptr].p_states.p_sts_num = 2; //assign the number of old states deriving the new state
    memcpy(g_states[repo_id]->states[s_new_ptr].p_states.p_sts[0], idx_old_1.s_id, STATE_ID_MAX); //assign the first old state as its dependency
    memcpy(g_states[repo_id]->states[s_new_ptr].p_states.p_sts[1], idx_old_2.s_id, STATE_ID_MAX); //assign the second old state as its dependency
  }

  {
    int coll_old_1_ptr = eget_coll_ptr(g_states[repo_id]->states[s_old_1_ptr].s_db, pred.colls[0]); // get the entry index of the first colloection to be processed
    if(coll_old_1_ptr == -1){
      return SGX_ERROR_INVALID_PARAMETER;
    }
    int coll_old_2_ptr = eget_coll_ptr(g_states[repo_id]->states[s_old_2_ptr].s_db, pred.colls[1]); // get the entry index of the second colloection to be processed
    if(coll_old_2_ptr == -1){
      return SGX_ERROR_INVALID_PARAMETER;
    }

    g_states[repo_id]->states[s_new_ptr].s_db.coll_num = g_states[repo_id]->states[s_old_1_ptr].s_db.coll_num;
    for(int i = 0; i < g_states[repo_id]->states[s_old_1_ptr].s_db.coll_num; i++){
      if(i == coll_old_1_ptr){ //processing this collection

        memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id, g_states[repo_id]->states[s_old_1_ptr].s_db.colls[i].coll_id, COLL_ID_MAX);
        int r = 0;
        for(int j = 0; j < g_states[repo_id]->states[s_old_1_ptr].s_db.colls[i].docs_num; j++){
          int attr_1_ptr = eget_attr_ptr(g_states[repo_id]->states[s_old_1_ptr].s_db.colls[i].docs[j], pred.attr_names[0]);
          if(attr_1_ptr == -1){
            continue;
          }

          for(int k = 0; k < g_states[repo_id]->states[s_old_2_ptr].s_db.colls[coll_old_2_ptr].docs_num; k++){

            int attr_2_ptr = eget_attr_ptr(g_states[repo_id]->states[s_old_2_ptr].s_db.colls[coll_old_2_ptr].docs[k], pred.attr_names[0]);
            if(attr_2_ptr == -1){
              continue;
            }

            if(strcmp(g_states[repo_id]->states[s_old_1_ptr].s_db.colls[i].docs[j].attrs[attr_1_ptr].value, g_states[repo_id]->states[s_old_2_ptr].s_db.colls[coll_old_2_ptr].docs[k].attrs[attr_2_ptr].value) == 0){
              doc_t doc_new_tmp;
              doc_new_tmp.attrs_num = 0;

              memcpy(&doc_new_tmp.attrs_num, &g_states[repo_id]->states[s_old_1_ptr].s_db.colls[i].docs[j].attrs_num, sizeof(doc_t));
              for(int q = 0; q < g_states[repo_id]->states[s_old_2_ptr].s_db.colls[coll_old_2_ptr].docs[k].attrs_num; q++){
                if(q != attr_2_ptr){
                  memcpy(&doc_new_tmp.attrs[doc_new_tmp.attrs_num++].name[0], &g_states[repo_id]->states[s_old_2_ptr].s_db.colls[coll_old_2_ptr].docs[k].attrs[q].name[0], sizeof(attr_t));
                }
              }

              memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs[r++].attrs_num, &doc_new_tmp.attrs_num, sizeof(doc_t));
            }
          }
        }
        g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs_num = r;

      }else{ //copy this collection to the new state
        memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id[0], &g_states[repo_id]->states[s_old_1_ptr].s_db.colls[i].coll_id[0], sizeof(coll_t));
      }
    }

    eprintf("\n+++++[DEBUG] new generated state: +++++\n");
    eprintst(g_states[repo_id]->states[s_new_ptr]);
  }


  memcpy((uint8_t *)s_out, &idx_new.repo_id, sizeof(state_idx_t));
  return SGX_SUCCESS;
}

sgx_status_t e_get_response(struct _state_idx_t s_in, void* res){

  sgx_status_t ret = SGX_SUCCESS;

  uint8_t repo_id = s_in.repo_id;
  char s_id[STATE_ID_MAX];
  memcpy(s_id, s_in.s_id, STATE_ID_MAX);

  if(!g_states[repo_id]->is_occupied){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  int s_ptr = eget_st_ptr(*g_states[repo_id], s_id); // get the entry index of the final state
  if(s_ptr == -1){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  int coll_ptr = eget_coll_ptr(g_states[repo_id]->states[s_ptr].s_db, "CAGG"); // get the entry index of the collection with id CAGG
  if(coll_ptr == -1){
    return SGX_ERROR_INVALID_PARAMETER;
  }

  doc_t res_pt;
  memcpy(&res_pt.attrs_num, &g_states[repo_id]->states[s_ptr].s_db.colls[coll_ptr].docs[0].attrs_num, sizeof(doc_t)); // get the result in plaintext

  sgx_aes_gcm_128bit_key_t sk = { 0x72, 0xee, 0x30, 0xb0, \
                                  0x1d, 0xd9, 0x11, 0x38, \
                                  0x24, 0x11, 0x14, 0x3a, \
                                  0xe2, 0xaa, 0x60, 0x38};
  uint8_t aes_gcm_iv[AES_IV_SIZE] = {0};
  aes_gcm_data_t *res_ct = (aes_gcm_data_t *)malloc(sizeof(aes_gcm_data_t) + sizeof(res_pt));
  memset(res_ct, 0, sizeof(aes_gcm_data_t) + sizeof(res_pt));
  ret = sgx_rijndael128GCM_encrypt(&sk, &res_pt.attrs_num, \
                                                  sizeof(res_pt), \
                                                  res_ct->payload, \
                                                  &aes_gcm_iv[0], \
                                                  AES_IV_SIZE, \
                                                  NULL, \
                                                  0, \
                                                  &res_ct->payload_tag);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: aes encrypt result failed\n");
    return ret;
  }
  res_ct->payload_size = sizeof(res_pt);

  //get the trust proof
  proof_t proof;
  proof.st_proof_num = s_ptr + 1;
  for(int i = 0; i <= s_ptr; i++){
    memcpy(proof.st_proofs[i].s_id, g_states[repo_id]->states[i].s_id, STATE_ID_MAX);
    memcpy(proof.st_proofs[i].f.func_name, g_states[repo_id]->states[i].f.func_name, FUN_NAME_MAX);
    memcpy(&proof.st_proofs[i].p_states.p_sts_num, &g_states[repo_id]->states[i].p_states.p_sts_num, sizeof(pre_states_t));
  }

  //sign the proof
  sgx_ecc_state_handle_t ecc_handle;
  sgx_ec256_signature_t p_signature;
  ret = sgx_ecc256_open_context(&ecc_handle);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: ecc256 context open failed\n");
    return ret;
  }
  ret = sgx_ecdsa_sign(&proof.st_proof_num, sizeof(proof_t), g_ecdsa_sign_key, &p_signature, ecc_handle);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: ecc256 sign proof failed\n");
    return ret;
  }
  ret = sgx_ecc256_close_context(ecc_handle);
  if(SGX_SUCCESS != ret){
    eprintf("[Err]: ecc256 context close failed\n");
    return ret;
  }

  //assimble response
  memcpy(&((response_t *)res)->pf.st_proof_num, &proof.st_proof_num, sizeof(proof_t));
  memcpy(((response_t *)res)->pf_sign.x, p_signature.x, (sizeof(sgx_ec256_signature_t) / 4));

  g_states[repo_id]->is_occupied = false; // release the memory region storing states for the current request
  memset(&g_states[repo_id]->states_num, 0, sizeof(states_t));

  free(res_ct);
  return SGX_SUCCESS;
}
