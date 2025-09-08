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

static long long parse_binary(const char *p) {
   long long ret = 0;
   p += 2;
   while (*p) {
      ret <<= 1;
      ret |= (*p - '0');
      p++;
   }
   return ret;
}

static void deunderscore(char *buf, const char *p) {
   char *q = buf;

   while (*p) {
      if (*p != '_') {
         *q++ = *p++;
      }
      else {
         p++;
      }
   }
   *q = 0;
}

static long long parse_int(const char *p) {
   char buf[256];
   deunderscore(buf, p);

   if (buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
      return parse_binary(buf);
   }

   return atoll(buf);
}

static double parse_float(const char *p) {
   char buf[256];
   deunderscore(buf, p);

   return atof(buf);
}

static double parse_node_to_double(ASTNode *node) {
   if (!strcmp(node->name, "int")) {
      return parse_int(node->strval);
   }
   else {
      return parse_float(node->strval);
   }
}

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

static bool is_unary_op(ASTNode *node) {
   if (node->name[1] == 0 && NULL != strchr("+-", node->name[0])) {
      if (node->count == 1) {
         if (is_lone_expr(node->children[0])) {
            node->children[0] = node->children[0]->children[0];
         }

         if (is_float_or_int(node->children[0])) {
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
      long long left = parse_int(node->children[0]->strval);
      long long right = parse_int(node->children[1]->strval);

      long long result = left + right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = parse_node_to_double(node->children[0]);
      double right = parse_node_to_double(node->children[1]);

      double result = left + right;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_unary_plus(ASTNode **noderef) {
   *noderef = (*noderef)->children[0];
}

static void handle_binary_minus(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      long long left = parse_int(node->children[0]->strval);
      long long right = parse_int(node->children[1]->strval);

      long long result = left - right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = parse_node_to_double(node->children[0]);
      double right = parse_node_to_double(node->children[1]);

      double result = left - right;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_unary_minus(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0])) {
      long long left = parse_int(node->children[0]->strval);

      long long result = -left;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = parse_node_to_double(node->children[0]);
      double result = -left;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_binary_times(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      long long left = parse_int(node->children[0]->strval);
      long long right = parse_int(node->children[1]->strval);

      long long result = left * right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = parse_node_to_double(node->children[0]);
      double right = parse_node_to_double(node->children[1]);

      double result = left * right;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_binary_divide(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      long long left = parse_int(node->children[0]->strval);
      long long right = parse_int(node->children[1]->strval);

      long long result = left / right;
      sprintf(buf, "%lld", result);
      *noderef = make_integer_leaf(strdup(buf));
   }
   else {
      double left = parse_node_to_double(node->children[0]);
      double right = parse_node_to_double(node->children[1]);

      double result = left / right;
      sprintf(buf, "%la", result);
      *noderef = make_float_leaf(strdup(buf));
   }
}

static void handle_binary_modulo(ASTNode **noderef) {
   ASTNode *node = *noderef;
   char buf[256];
   if (is_int(node->children[0]) && is_int(node->children[1])) {
      long long left = parse_int(node->children[0]->strval);
      long long right = parse_int(node->children[1]->strval);

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

   if (is_unary_op(node)) {
      switch(node->name[0]) {
         case '+':
            handle_unary_plus(noderef);
            return;
         case '-':
            handle_unary_minus(noderef);
            return;
      }
   }

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
