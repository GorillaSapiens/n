#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ast.h"
#include "expropt.h"
#include "float.h"
#include "integer.h"
#include "messages.h"
#include "xray.h"

static bool is_int(ASTNode *node) {
   if (!strcmp(node->name, "int")) {
      if (node->count == 0) { // backtick casting is done on the processor
         return true;
      }
   }
   return false;
}

static bool is_float_or_int(ASTNode *node) {
   if (!strcmp(node->name, "int") || !strcmp(node->name, "float")) {
      if (node->count == 0) { // backtick casting is done on the processor
         return true;
      }
   }
   return false;
}

static bool is_lone_expr(ASTNode *node) {
   if (!strcmp(node->name, "expr")) {
      if (node->count == 1) {
         if (is_float_or_int(node->children[0])) {
            return true;
         }
      }
   }
   return false;
}

static bool is_binary_op(ASTNode *node) {
   if (node->name[1] == 0 && NULL != strchr("+-*/%", node->name[0])) {
      if (node->count == 2) {
         if (is_lone_expr(node->children[0])) {
            node->children[0] = node->children[0]->children[0];
         }

         if (is_lone_expr(node->children[1])) {
            node->children[1] = node->children[1]->children[0];
         }

         if (is_float_or_int(node->children[0]) &&
             is_float_or_int(node->children[1])) {
            return true;
         }
      }
   }
   return false;
}

static void handle_binary_plus(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      // TODO FIX handle binary!
      long long left = atoll(node->children[0]->strval);
      long long right = atoll(node->children[1]->strval);

      long long result = left + right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = atof(node->children[0]->strval);
      double right = atof(node->children[1]->strval);

      double result = left + right;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_binary_minus(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      // TODO FIX handle binary!
      long long left = atoll(node->children[0]->strval);
      long long right = atoll(node->children[1]->strval);

      long long result = left - right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = atof(node->children[0]->strval);
      double right = atof(node->children[1]->strval);

      double result = left - right;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_binary_times(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      // TODO FIX handle binary!
      long long left = atoll(node->children[0]->strval);
      long long right = atoll(node->children[1]->strval);

      long long result = left * right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = atof(node->children[0]->strval);
      double right = atof(node->children[1]->strval);

      double result = left * right;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_binary_divide(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      // TODO FIX handle binary!
      long long left = atoll(node->children[0]->strval);
      long long right = atoll(node->children[1]->strval);

      long long result = left / right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = atof(node->children[0]->strval);
      double right = atof(node->children[1]->strval);

      double result = left / right;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_binary_modulo(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      // TODO FIX handle binary!
      long long left = atoll(node->children[0]->strval);
      long long right = atoll(node->children[1]->strval);

      long long result = left % right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      error("float modulo undefined at [%s:%d.%d]",
         node->file, node->line, node->column);
      // error calls exit
   }
}

static void expropt(ASTNode **noderef) {
   if (noderef == NULL) {
      return;
   }

   ASTNode *node = *noderef;

   if (*noderef == NULL) {
      return;
   }

   // binary operators
   if (is_binary_op(node)) {
      switch(node->name[0]) {
         case '+':
            handle_binary_plus(noderef);
            return;
         case '-':
            handle_binary_minus(noderef);
            return;
         case '*':
            handle_binary_times(noderef);
            return;
         case '/':
            handle_binary_divide(noderef);
            return;
         case '%':
            handle_binary_modulo(noderef);
            return;
      }
   }

#if 0
   // parenthetical expressions down to one term
   if (!strcmp(node->name, "expr") && node->count == 1) {
      if (is_float_or_int(node->children[0])) {
         *noderef = node->children[0];
         return;
      }
   }
#endif

   // everybody else
   for (int i = 0; i < node->count; i++) {
      expropt(&(node->children[i]));
   }
}

static int astcount(ASTNode *node) {
   int ret = 0;

   if (node) {
      ret++; // for us

      for(int i = 0; i < node->count; i++) {
         ret += astcount(node->children[i]);
      }
   }

   return ret;
}

void do_expropt(void) {
   int before, after;
   int pre, post, passes = 0;

   if (get_xray(XRAY_EXPROPTONLY)) {
      before = astcount(root);
      parse_dump();
   }

   do {
      pre = astcount(root);
      passes++;
      expropt(&root);
      post = astcount(root);
   } while (pre != post);

   if (get_xray(XRAY_EXPROPTONLY)) {
      after = astcount(root);

      parse_dump();
      message("expropt: %d -> %d, %d passes\n", before, after, passes);
   }
}
