#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>      /* vsnprintf */
#include <string.h>
#include <stdlib.h>

#include "enclave_u.h"
#include "naive_operators.h"

#include "sample_libcrypto.h"

states_t *g_states[REQ_PARALLELISM];

uint8_t u_key[16] = {0x9f, 0x86, 0x2f, 0x61,
                     0x36, 0x03, 0xe2, 0xc9,
                     0xe8, 0xee, 0xeb, 0x72,
                     0x28, 0x50, 0x26, 0xbd};

#define STATE_ID_NUM_MAX 9
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
  // {false, "S10"},
  // {false, "S11"},
  // {false, "S12"},
  // {false, "S13"},
  // {false, "S14"},
  // {false, "S15"},
  // {false, "S16"},
  // {false, "S17"},
  // {false, "S18"},
  // {false, "S19"},
  // {false, "S20"},
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

void printst(state_t st){
  printf("w: %d\n", st.w);
  printf("state ID: %s\n", st.s_id);
  printf("state generated by function: %s\n", st.f.func_name);
  printf("state derived from: ");
  for(int i = 0; i < st.p_states.p_sts_num; i++){
    printf("%s ", st.p_states.p_sts[i]);
  }
  printf("\n");
  printf("state workload (total %d collections):\n\n", st.s_db.coll_num);
  for(int i = 0; i < st.s_db.coll_num; i++){
    printf("the %dth collection %s (total %d documents):\n", (i+1), st.s_db.colls[i].coll_id, st.s_db.colls[i].docs_num);
    for(int j = 0; j < st.s_db.colls[i].docs_num; j++){
      printf("the %dth document:", (j+1));
      for(int k = 0; k < st.s_db.colls[i].docs[j].attrs_num; k++){
        printf("(%s: %s) ", st.s_db.colls[i].docs[j].attrs[k].name, st.s_db.colls[i].docs[j].attrs[k].value);
      }
      printf("\n");
    }
    printf("\n");
  }
}

bool states_init(uint8_t* msg, void* idx_out){
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
    if(i == REQ_PARALLELISM){
      return false;
    }
    g_states[idx_tmp.repo_id]->states_num = 1;
    g_states[idx_tmp.repo_id]->is_occupied = true;
    g_states[idx_tmp.repo_id]->states[0].w = 5; //test
    char id[] = "S0";
    strncpy(g_states[idx_tmp.repo_id]->states[0].s_id, id, sizeof(id));
    strncpy(g_states[idx_tmp.repo_id]->states[0].f.func_name, __FUNCTION__, sizeof(__FUNCTION__));
    memcpy(&g_states[idx_tmp.repo_id]->states[0].s_db.coll_num, &((coll_db_t *)msg)->coll_num, sizeof(coll_db_t));
    strncpy(idx_tmp.s_id, id, sizeof(id));
    memcpy((uint8_t *)idx_out, &idx_tmp.repo_id, sizeof(state_idx_t));

    // printf("\n+++++[DEBUG] initialize state: +++++\n");
    // printst(g_states[idx_tmp.repo_id]->states[0]);
  }

  return true;
}

bool selector(pred_t s_pred, state_idx_t s_in, void* s_out){
  if(s_pred.tp != 2 || s_pred.attrs_num != 1 || s_pred.colls_num != 1 \
      || s_in.repo_id >= REQ_PARALLELISM \
        || !g_states[s_in.repo_id]->is_occupied){
    return false;
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
      return false;
    }
    memcpy(idx_new.s_id, id, STATE_ID_MAX);
    free(id);
  }

  uint8_t repo_id = idx_new.repo_id; // get the ID of current repository
  if(g_states[repo_id]->states[0].w == 0){ //the states can not be access any more
    return false;
  }

  int s_old_ptr = eget_st_ptr(*g_states[repo_id], idx_old.s_id); // get the entry index of the old state
  if(s_old_ptr == -1){
    return false;
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
      return false;
    }
    g_states[repo_id]->states[s_new_ptr].s_db.coll_num = g_states[repo_id]->states[s_old_ptr].s_db.coll_num;
    for(int i = 0; i < g_states[repo_id]->states[s_old_ptr].s_db.coll_num; i++){
      if(i == coll_old_ptr){ //processing this collection

        memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id, g_states[repo_id]->states[s_old_ptr].s_db.colls[i].coll_id, sizeof(COLL_ID_MAX));

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

    // printf("\n+++++[DEBUG] new generated state: +++++\n");
    // printst(g_states[repo_id]->states[s_new_ptr]);
  }

  memcpy((uint8_t *)s_out, &idx_new.repo_id, sizeof(state_idx_t));

  return true;
}

bool projector(pred_t p_pred, state_idx_t s_in, void* s_out){
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
      return false;
    }
    memcpy(idx_new.s_id, id, STATE_ID_MAX);
    free(id);
  }

  uint8_t repo_id = idx_new.repo_id; // get the ID of current repository
  if(g_states[repo_id]->states[0].w == 0){ //the states can not be access any more
    return false;
  }

  int s_old_ptr = eget_st_ptr(*g_states[repo_id], idx_old.s_id); // get the entry index of the old state
  if(s_old_ptr == -1){
    return false;
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
      return false;
    }

    g_states[repo_id]->states[s_new_ptr].s_db.coll_num = g_states[repo_id]->states[s_old_ptr].s_db.coll_num;
    for(int i = 0; i < g_states[repo_id]->states[s_old_ptr].s_db.coll_num; i++){
      if(i == coll_old_ptr){ //processing this collection
        memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id, g_states[repo_id]->states[s_old_ptr].s_db.colls[i].coll_id, sizeof(COLL_ID_MAX));
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

    // printf("\n+++++[DEBUG] new generated state: +++++\n");
    // printst(g_states[repo_id]->states[s_new_ptr]);
  }

  memcpy((uint8_t *)s_out, &idx_new.repo_id, sizeof(state_idx_t));
  return true;
}

bool aggregator(pred_t a_pred, state_idx_t s_in, void* s_out){

    if(a_pred.tp != 3 || a_pred.attrs_num != 1 || a_pred.colls_num != 1\
        || s_in.repo_id >= REQ_PARALLELISM \
          || !g_states[s_in.repo_id]->is_occupied){
      return false;
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
        return false;
      }
      memcpy(idx_new.s_id, id, STATE_ID_MAX);
      free(id);
    }

    uint8_t repo_id = idx_new.repo_id; // get the ID of current repository
    if(g_states[repo_id]->states[0].w == 0){ //the states can not be access any more
      return false;
    }

    int s_old_ptr = eget_st_ptr(*g_states[repo_id], idx_old.s_id); // get the entry index of the old state
    if(s_old_ptr == -1){
      return false;
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
        return false;
      }

      g_states[repo_id]->states[s_new_ptr].s_db.coll_num = 1;
      g_states[repo_id]->states[s_new_ptr].s_db.colls[0].docs_num = 1;
      memcpy(g_states[repo_id]->states[s_new_ptr].s_db.colls[0].coll_id, "AG", 3);
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
        return false;
      }

      // printf("\n+++++[DEBUG] new generated state: +++++\n");
      // printst(g_states[repo_id]->states[s_new_ptr]);
    }

    memcpy((uint8_t *)s_out, &idx_new.repo_id, sizeof(state_idx_t));
    return true;
}

bool joiner(pred_t j_pred, state_idx_t s_in_1, state_idx_t s_in_2, void* s_out){
  if(j_pred.tp != 4 || j_pred.attrs_num == 0 || j_pred.colls_num != 2 \
      || s_in_1.repo_id != s_in_2.repo_id || s_in_1.repo_id >= REQ_PARALLELISM \
        || !g_states[s_in_1.repo_id]->is_occupied){
    return false;
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
      return false;
    }
    memcpy(idx_new.s_id, id, STATE_ID_MAX);
    free(id);
  }

  uint8_t repo_id = idx_new.repo_id; // get the ID of current repository
  if(g_states[repo_id]->states[0].w == 0){ //the states can not be access any more
    return false;
  }

  int s_old_1_ptr = eget_st_ptr(*g_states[repo_id], idx_old_1.s_id); // get the entry index of the old state
  if(s_old_1_ptr == -1){
    return false;
  }

  int s_old_2_ptr = eget_st_ptr(*g_states[repo_id], idx_old_2.s_id); // get the entry index of the old state
  if(s_old_2_ptr == -1){
    return false;
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
      return false;
    }
    int coll_old_2_ptr = eget_coll_ptr(g_states[repo_id]->states[s_old_2_ptr].s_db, pred.colls[1]); // get the entry index of the second colloection to be processed
    if(coll_old_2_ptr == -1){
      return false;
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

              memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs[r].attrs_num, &doc_new_tmp.attrs_num, sizeof(doc_t));
              r++;
            }
          }
        }
        g_states[repo_id]->states[s_new_ptr].s_db.colls[i].docs_num = r;

      }else{ //copy this collection to the new state
        memcpy(&g_states[repo_id]->states[s_new_ptr].s_db.colls[i].coll_id[0], &g_states[repo_id]->states[s_old_1_ptr].s_db.colls[i].coll_id[0], sizeof(coll_t));
      }
    }

    // printf("\n+++++[DEBUG] new generated state: +++++\n");
    // printst(g_states[repo_id]->states[s_new_ptr]);
  }


  memcpy((uint8_t *)s_out, &idx_new.repo_id, sizeof(state_idx_t));
  return true;
}

void write_cipher(const char *f_n, void *ct, int ct_size){
  FILE *out = fopen(f_n, "wb");
  if (NULL == out){
    printf("cannot open file %s\n", f_n);
    return;
  }
  fwrite(ct, ct_size, 1, out);
  fclose(out);
  return;
}

bool store(uint8_t* msg, size_t msg_size, const char* file_name){

  uint8_t aes_gcm_iv[AES_IV_SIZE] = {0};

  aes_gcm_data_t *ct = (aes_gcm_data_t *)malloc(sizeof(aes_gcm_data_t) + msg_size);
  memset(ct, 0, sizeof(aes_gcm_data_t)+msg_size);

  int ret = sample_rijndael128GCM_encrypt(&u_key,
                                          &((coll_db_t *)msg)->coll_num,
                                          msg_size,
                                          ct->payload,
                                          &aes_gcm_iv[0],
                                          AES_IV_SIZE,
                                          NULL,
                                          0,
                                          &ct->payload_tag);
  ct->payload_size = msg_size;

  if(SAMPLE_SUCCESS != ret){
    printf("data store error!!!\n");
    return false;
  }

  write_cipher(file_name, (void *)ct, sizeof(aes_gcm_data_t)+msg_size);

  return true;
}
