%{
#include "ASTNodes.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "utils.h"

// 语法分析结束后，整个程序的语法树根节点
NBlock* programBlock = nullptr;

extern int yylex();
extern int currentLine;

void yyerror(const char* s) {
    fprintf(stderr, "error: 第 %d 行: %s\n", currentLine, s);
}
%}

%union {
    NBlock* block;
    NExpression* expr;
    NStatement* stmt;
    NIdentifier* ident;
    NTypeRef* type;
    NVariableDeclaration* var_decl;
    NArrayIndex* index;
    std::vector<std::shared_ptr<NVariableDeclaration>>* varvec;
    std::vector<std::shared_ptr<NExpression>>* exprvec;
    std::string* text;
    int token;
}

%token <text> TIDENTIFIER TINTEGER TDOUBLE TYINT TYDOUBLE TYFLOAT TYCHAR TYBOOL TYVOID TYSTRING TEXTERN TLITERAL
%token <token> TCEQ TCNE TCLT TCLE TCGT TCGE TEQUAL
%token <token> TLPAREN TRPAREN TLBRACE TRBRACE TCOMMA TDOT TSEMICOLON TLBRACKET TRBRACKET
%token <token> TPLUS TMINUS TMUL TDIV TAND TOR TXOR TMOD TSHIFTL TSHIFTR
%token <token> TIF TELSE TFOR TWHILE TRETURN TSTRUCT

%type <index> array_index
%type <ident> ident
%type <type> primary_typename array_typename struct_typename typename
%type <expr> numeric expr assign
%type <varvec> func_decl_args struct_members
%type <exprvec> call_args
%type <block> program stmts block
%type <stmt> stmt var_decl array_decl func_decl struct_decl if_stmt for_stmt while_stmt
%type <var_decl> param_decl
%type <token> comparison

/* 运算符优先级，从低到高，和 C 语言保持一致 */
%left TPLUS TMINUS
%left TMUL TDIV TMOD
%left TSHIFTL TSHIFTR
%left TAND
%left TXOR
%left TOR
%left TCEQ TCNE TCLT TCLE TCGT TCGE

%start program

%%

program : stmts { programBlock = $1; }
        ;

stmts : stmt { $$ = new NBlock(); $$->statements.push_back(std::shared_ptr<NStatement>($1)); }
      | stmts stmt { $1->statements.push_back(std::shared_ptr<NStatement>($2)); }
      ;

stmt : var_decl
     | array_decl
     | func_decl
     | struct_decl
     | expr { $$ = new NExpressionStatement(std::shared_ptr<NExpression>($1)); }
     | TRETURN expr { $$ = new NReturnStatement(std::shared_ptr<NExpression>($2)); }
     | if_stmt
     | for_stmt
     | while_stmt
     ;

block : TLBRACE stmts TRBRACE { $$ = $2; }
      | TLBRACE TRBRACE { $$ = new NBlock(); }
      ;

/* ---------------------------------------------------------------- 类型 */

primary_typename : TYINT    { $$ = new NTypeRef(*$1); delete $1; }
                | TYDOUBLE { $$ = new NTypeRef(*$1); delete $1; }
                | TYFLOAT  { $$ = new NTypeRef(*$1); delete $1; }
                | TYCHAR   { $$ = new NTypeRef(*$1); delete $1; }
                | TYBOOL   { $$ = new NTypeRef(*$1); delete $1; }
                | TYVOID   { $$ = new NTypeRef(*$1); delete $1; }
                | TYSTRING { $$ = new NTypeRef(*$1); delete $1; }
                ;

array_typename : primary_typename TLBRACKET TINTEGER TRBRACKET {
                    $1->dims.push_back(strtoull($3->c_str(), nullptr, 10));
                    delete $3;
                    $$ = $1;
                 }
                | array_typename TLBRACKET TINTEGER TRBRACKET {
                    $1->dims.push_back(strtoull($3->c_str(), nullptr, 10));
                    delete $3;
                    $$ = $1;
                 }
                ;

struct_typename : TSTRUCT ident { $$ = new NTypeRef($2->name); delete $2; }
                ;

typename : primary_typename { $$ = $1; }
         | array_typename { $$ = $1; }
         | struct_typename { $$ = $1; }
         ;

/* ------------------------------------------------------------ 变量声明 */

/* param_decl 是最普通的变量声明，用在函数参数和结构体成员上 */
param_decl : typename ident {
                $$ = new NVariableDeclaration(std::shared_ptr<NTypeRef>($1),
                                             std::shared_ptr<NIdentifier>($2), nullptr);
            }
            | typename ident TEQUAL expr {
                $$ = new NVariableDeclaration(std::shared_ptr<NTypeRef>($1),
                                             std::shared_ptr<NIdentifier>($2),
                                             std::shared_ptr<NExpression>($4));
            }
            ;

var_decl : param_decl { $$ = $1; }
         ;

array_decl : typename ident TEQUAL TLBRACKET call_args TRBRACKET {
                 auto declaration = std::make_shared<NVariableDeclaration>(
                     std::shared_ptr<NTypeRef>($1), std::shared_ptr<NIdentifier>($2), nullptr);
                 std::vector<std::shared_ptr<NExpression>> elements = std::move(*$5);
                 delete $5;
                 $$ = new NArrayInitialization(declaration, std::move(elements));
             }
             ;

/* ------------------------------------------------------------ 函数声明 */

func_decl : typename ident TLPAREN func_decl_args TRPAREN block {
               std::vector<std::shared_ptr<NVariableDeclaration>> args = std::move(*$4);
               delete $4;
               $$ = new NFunctionDeclaration(std::shared_ptr<NTypeRef>($1),
                                             std::shared_ptr<NIdentifier>($2),
                                             std::move(args),
                                             std::shared_ptr<NBlock>($6));
           }
           | TEXTERN typename ident TLPAREN func_decl_args TRPAREN {
               std::vector<std::shared_ptr<NVariableDeclaration>> args = std::move(*$5);
               delete $5;
               $$ = new NFunctionDeclaration(std::shared_ptr<NTypeRef>($2),
                                             std::shared_ptr<NIdentifier>($3),
                                             std::move(args), nullptr, true);
           }
           ;

func_decl_args : /* empty */ {
                     $$ = new std::vector<std::shared_ptr<NVariableDeclaration>>();
                 }
                | param_decl {
                     $$ = new std::vector<std::shared_ptr<NVariableDeclaration>>();
                     $$->push_back(std::shared_ptr<NVariableDeclaration>($1));
                 }
                | func_decl_args TCOMMA param_decl {
                     $1->push_back(std::shared_ptr<NVariableDeclaration>($3));
                 }
                ;

ident : TIDENTIFIER { $$ = new NIdentifier(*$1); delete $1; }
      ;

/* -------------------------------------------------------------- 表达式 */

numeric : TINTEGER { $$ = new NInteger(strtoll($1->c_str(), nullptr, 10)); delete $1; }
        | TDOUBLE { $$ = new NDouble(atof($1->c_str())); delete $1; }
        ;

expr : assign { $$ = $1; }
     | ident TLPAREN call_args TRPAREN {
           std::vector<std::shared_ptr<NExpression>> args = std::move(*$3);
           delete $3;
           $$ = new NMethodCall(std::shared_ptr<NIdentifier>($1), std::move(args));
       }
     | ident { $$ = $1; }
     | ident TDOT ident {
           $$ = new NStructMember(std::shared_ptr<NIdentifier>($1),
                                  std::shared_ptr<NIdentifier>($3));
       }
     | numeric
     | expr comparison expr {
           $$ = new NBinaryOperator(std::shared_ptr<NExpression>($1),
                                    static_cast<BinOp>($2),
                                    std::shared_ptr<NExpression>($3));
       }
     | expr TMOD expr {
           $$ = new NBinaryOperator(std::shared_ptr<NExpression>($1), OP_MOD,
                                    std::shared_ptr<NExpression>($3));
       }
     | expr TMUL expr {
           $$ = new NBinaryOperator(std::shared_ptr<NExpression>($1), OP_MUL,
                                    std::shared_ptr<NExpression>($3));
       }
     | expr TDIV expr {
           $$ = new NBinaryOperator(std::shared_ptr<NExpression>($1), OP_DIV,
                                    std::shared_ptr<NExpression>($3));
       }
     | expr TPLUS expr {
           $$ = new NBinaryOperator(std::shared_ptr<NExpression>($1), OP_ADD,
                                    std::shared_ptr<NExpression>($3));
       }
     | expr TMINUS expr {
           $$ = new NBinaryOperator(std::shared_ptr<NExpression>($1), OP_SUB,
                                    std::shared_ptr<NExpression>($3));
       }
     | TLPAREN expr TRPAREN { $$ = $2; }
     /* 一元负号改写成 0 - expr */
     | TMINUS expr {
           $$ = new NBinaryOperator(std::make_shared<NInteger>(0), OP_SUB,
                                    std::shared_ptr<NExpression>($2));
       }
     | array_index { $$ = $1; }
     | TLITERAL { $$ = new NLiteral(*$1); delete $1; }
     ;

array_index : ident TLBRACKET expr TRBRACKET {
                   $$ = new NArrayIndex(std::shared_ptr<NIdentifier>($1),
                                        std::vector<std::shared_ptr<NExpression>>{std::shared_ptr<NExpression>($3)});
               }
              | array_index TLBRACKET expr TRBRACKET {
                   $1->exprs.push_back(std::shared_ptr<NExpression>($3));
                   $$ = $1;
               }
              ;

assign : ident TEQUAL expr {
             $$ = new NAssignment(std::shared_ptr<NIdentifier>($1),
                                  std::shared_ptr<NExpression>($3));
         }
        | array_index TEQUAL expr {
             $$ = new NArrayAssignment(std::shared_ptr<NArrayIndex>($1),
                                       std::shared_ptr<NExpression>($3));
         }
        | ident TDOT ident TEQUAL expr {
             auto member = std::make_shared<NStructMember>(std::shared_ptr<NIdentifier>($1),
                                                           std::shared_ptr<NIdentifier>($3));
             $$ = new NStructAssignment(member, std::shared_ptr<NExpression>($5));
         }
        ;

call_args : /* empty */ { $$ = new std::vector<std::shared_ptr<NExpression>>(); }
           | expr {
               $$ = new std::vector<std::shared_ptr<NExpression>>();
               $$->push_back(std::shared_ptr<NExpression>($1));
           }
           | call_args TCOMMA expr { $1->push_back(std::shared_ptr<NExpression>($3)); }
           ;

/* comparison 的值就是 BinOp，方便后面直接生成代码 */
comparison : TCEQ { $$ = OP_EQ; }
           | TCNE { $$ = OP_NE; }
           | TCLT { $$ = OP_LT; }
           | TCLE { $$ = OP_LE; }
           | TCGT { $$ = OP_GT; }
           | TCGE { $$ = OP_GE; }
           | TAND { $$ = OP_AND; }
           | TOR { $$ = OP_OR; }
           | TXOR { $$ = OP_XOR; }
           | TSHIFTL { $$ = OP_SHL; }
           | TSHIFTR { $$ = OP_SHR; }
           ;

/* -------------------------------------------------------------- 控制流 */

if_stmt : TIF expr block {
               $$ = new NIfStatement(std::shared_ptr<NExpression>($2),
                                     std::shared_ptr<NBlock>($3));
           }
        | TIF expr block TELSE block {
               $$ = new NIfStatement(std::shared_ptr<NExpression>($2),
                                     std::shared_ptr<NBlock>($3),
                                     std::shared_ptr<NBlock>($5));
           }
        | TIF expr block TELSE if_stmt {
               auto block = std::make_shared<NBlock>();
               block->statements.push_back(std::shared_ptr<NStatement>($5));
               $$ = new NIfStatement(std::shared_ptr<NExpression>($2),
                                     std::shared_ptr<NBlock>($3), block);
           }
        ;

for_stmt : TFOR TLPAREN expr TSEMICOLON expr TSEMICOLON expr TRPAREN block {
                $$ = new NForStatement(std::shared_ptr<NBlock>($9),
                                       std::shared_ptr<NExpression>($3),
                                       std::shared_ptr<NExpression>($5),
                                       std::shared_ptr<NExpression>($7));
            }
         ;

while_stmt : TWHILE TLPAREN expr TRPAREN block {
                 $$ = new NForStatement(std::shared_ptr<NBlock>($5), nullptr,
                                        std::shared_ptr<NExpression>($3), nullptr);
             }
         ;

/* ------------------------------------------------------------ 结构体 */

struct_decl : TSTRUCT ident TLBRACE struct_members TRBRACE {
                 std::vector<std::shared_ptr<NVariableDeclaration>> members = std::move(*$4);
                 delete $4;
                 $$ = new NStructDeclaration(std::shared_ptr<NIdentifier>($2),
                                             std::move(members));
             }
             ;

struct_members : /* empty */ { $$ = new std::vector<std::shared_ptr<NVariableDeclaration>>(); }
                | param_decl {
                    $$ = new std::vector<std::shared_ptr<NVariableDeclaration>>();
                    $$->push_back(std::shared_ptr<NVariableDeclaration>($1));
                }
                | struct_members param_decl {
                    $1->push_back(std::shared_ptr<NVariableDeclaration>($2));
                }
                ;

%%
