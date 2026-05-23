// ============================================================
//  ta-gcc — TA64 C Compiler Frontend (prototype)
//  Parses a tiny subset of C and emits TA64 assembly.
//
//  Supported:
//    int main() { ... }
//    int x = expr;
//    return expr;
//    expr: int literals, variables, +, -, *, /, function calls
//    if (cond) { ... }
//    while (cond) { ... }
//    int func(int a, int b) { ... }
// ============================================================
#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// ────────────────────────────────────────────────────────────
//  LEXER
// ────────────────────────────────────────────────────────────
enum class TK {
    INT_LIT, IDENT, KW_INT, KW_RETURN, KW_IF, KW_ELSE, KW_WHILE,
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQ, EQEQ, NEQ, LT, GT, LEQ, GEQ,
    AMP, PIPE, CARET, TILDE, BANG,
    LPAREN, RPAREN, LBRACE, RBRACE, SEMI, COMMA,
    AND_AND, OR_OR,
    PLUSEQ, MINUSEQ,
    PLUSPLUS, MINUSMINUS,
    END
};

struct Token { TK type; std::string val; int line; };

class Lexer {
public:
    Lexer(const std::string& src) : src_(src) {}
    std::vector<Token> tokenise() {
        std::vector<Token> toks;
        while (pos_ < src_.size()) {
            skip_ws();
            if (pos_ >= src_.size()) break;
            char c = src_[pos_];
            // Comment
            if (c == '/' && peek(1) == '/') {
                while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
                continue;
            }
            if (c == '/' && peek(1) == '*') {
                pos_ += 2;
                while (pos_+1 < src_.size() && !(src_[pos_]=='*' && src_[pos_+1]=='/')) {
                    if (src_[pos_] == '\n') ++line_;
                    ++pos_;
                }
                pos_ += 2;
                continue;
            }
            if (std::isdigit(c)) { toks.push_back(read_int()); continue; }
            if (std::isalpha(c) || c == '_') { toks.push_back(read_ident()); continue; }
            toks.push_back(read_punct());
        }
        toks.push_back({TK::END, "", line_});
        return toks;
    }
private:
    std::string src_;
    size_t pos_ = 0;
    int    line_ = 1;

    char peek(int off=0) {
        return (pos_+off < src_.size()) ? src_[pos_+off] : '\0';
    }
    void skip_ws() {
        while (pos_ < src_.size() && std::isspace(src_[pos_])) {
            if (src_[pos_]=='\n') ++line_;
            ++pos_;
        }
    }
    Token read_int() {
        std::string s;
        while (pos_ < src_.size() && std::isdigit(src_[pos_])) s += src_[pos_++];
        return {TK::INT_LIT, s, line_};
    }
    Token read_ident() {
        std::string s;
        while (pos_ < src_.size() && (std::isalnum(src_[pos_]) || src_[pos_]=='_'))
            s += src_[pos_++];
        TK t = TK::IDENT;
        if (s=="int")    t = TK::KW_INT;
        else if (s=="return") t = TK::KW_RETURN;
        else if (s=="if")     t = TK::KW_IF;
        else if (s=="else")   t = TK::KW_ELSE;
        else if (s=="while")  t = TK::KW_WHILE;
        return {t, s, line_};
    }
    Token read_punct() {
        char c = src_[pos_++];
        auto ch2 = [&](char x, TK t2, TK t1) {
            if (peek()==x) { ++pos_; return t2; } return t1;
        };
        switch (c) {
        case '(': return {TK::LPAREN,"(",line_};
        case ')': return {TK::RPAREN,")",line_};
        case '{': return {TK::LBRACE,"{",line_};
        case '}': return {TK::RBRACE,"}",line_};
        case ';': return {TK::SEMI,";",line_};
        case ',': return {TK::COMMA,",",line_};
        case '+':
            if (peek()=='+') { ++pos_; return {TK::PLUSPLUS,"++",line_}; }
            if (peek()=='=') { ++pos_; return {TK::PLUSEQ,"+=",line_}; }
            return {TK::PLUS,"+",line_};
        case '-':
            if (peek()=='-') { ++pos_; return {TK::MINUSMINUS,"--",line_}; }
            if (peek()=='=') { ++pos_; return {TK::MINUSEQ,"-=",line_}; }
            return {TK::MINUS,"-",line_};
        case '*': return {TK::STAR,"*",line_};
        case '/': return {TK::SLASH,"/",line_};
        case '%': return {TK::PERCENT,"%",line_};
        case '=':
            if (peek()=='=') { ++pos_; return {TK::EQEQ,"==",line_}; }
            return {TK::EQ,"=",line_};
        case '!':
            if (peek()=='=') { ++pos_; return {TK::NEQ,"!=",line_}; }
            return {TK::BANG,"!",line_};
        case '<':
            if (peek()=='=') { ++pos_; return {TK::LEQ,"<=",line_}; }
            return {TK::LT,"<",line_};
        case '>':
            if (peek()=='=') { ++pos_; return {TK::GEQ,">=",line_}; }
            return {TK::GT,">",line_};
        case '&':
            if (peek()=='&') { ++pos_; return {TK::AND_AND,"&&",line_}; }
            return {TK::AMP,"&",line_};
        case '|':
            if (peek()=='|') { ++pos_; return {TK::OR_OR,"||",line_}; }
            return {TK::PIPE,"|",line_};
        case '^': return {TK::CARET,"^",line_};
        case '~': return {TK::TILDE,"~",line_};
        default:
            throw std::runtime_error(std::string("Unknown char: ") + c);
        }
    }
};

// ────────────────────────────────────────────────────────────
//  AST
// ────────────────────────────────────────────────────────────
struct ASTNode;
using ASTPtr = std::unique_ptr<ASTNode>;

enum class NodeKind {
    Prog, FuncDef, Block,
    VarDecl, Assign, Return,
    If, While,
    BinOp, UnOp, IntLit, Var, Call,
    PreInc, PreDec
};

struct ASTNode {
    NodeKind kind;
    std::string sval;        // name / operator
    int         ival = 0;   // int literal value
    std::vector<ASTPtr> children;

    static ASTPtr make(NodeKind k, std::string s="", int v=0) {
        auto n = std::make_unique<ASTNode>();
        n->kind = k; n->sval = std::move(s); n->ival = v;
        return n;
    }
};

// ────────────────────────────────────────────────────────────
//  PARSER
// ────────────────────────────────────────────────────────────
class Parser {
public:
    Parser(std::vector<Token> toks) : toks_(std::move(toks)), pos_(0) {}

    ASTPtr parse_program() {
        auto prog = ASTNode::make(NodeKind::Prog);
        while (cur().type != TK::END)
            prog->children.push_back(parse_function());
        return prog;
    }

private:
    std::vector<Token> toks_;
    size_t pos_;

    Token& cur()  { return toks_[pos_]; }
    Token& peek(int n=1) { return toks_[std::min(pos_+n, toks_.size()-1)]; }
    Token consume() { return toks_[pos_++]; }
    Token expect(TK t, const std::string& msg="") {
        if (cur().type != t)
            throw std::runtime_error("Parse error line " + std::to_string(cur().line)
                + ": expected " + msg + " got '" + cur().val + "'");
        return consume();
    }

    ASTPtr parse_function() {
        expect(TK::KW_INT, "int");
        std::string name = expect(TK::IDENT, "name").val;
        expect(TK::LPAREN, "(");
        // Parameter list
        std::vector<std::string> params;
        while (cur().type != TK::RPAREN) {
            expect(TK::KW_INT, "int");
            params.push_back(expect(TK::IDENT, "param name").val);
            if (cur().type == TK::COMMA) consume();
        }
        expect(TK::RPAREN, ")");
        auto body = parse_block();
        auto fn = ASTNode::make(NodeKind::FuncDef, name);
        for (auto& p : params) {
            auto pnode = ASTNode::make(NodeKind::Var, p);
            fn->children.push_back(std::move(pnode));
        }
        fn->children.push_back(std::move(body));
        return fn;
    }

    ASTPtr parse_block() {
        expect(TK::LBRACE, "{");
        auto blk = ASTNode::make(NodeKind::Block);
        while (cur().type != TK::RBRACE && cur().type != TK::END)
            blk->children.push_back(parse_statement());
        expect(TK::RBRACE, "}");
        return blk;
    }

    ASTPtr parse_statement() {
        // if
        if (cur().type == TK::KW_IF) {
            consume();
            expect(TK::LPAREN,"(");
            auto cond = parse_expr();
            expect(TK::RPAREN,")");
            auto then = parse_block();
            auto node = ASTNode::make(NodeKind::If);
            node->children.push_back(std::move(cond));
            node->children.push_back(std::move(then));
            if (cur().type == TK::KW_ELSE) {
                consume();
                node->children.push_back(parse_block());
            }
            return node;
        }
        // while
        if (cur().type == TK::KW_WHILE) {
            consume();
            expect(TK::LPAREN,"(");
            auto cond = parse_expr();
            expect(TK::RPAREN,")");
            auto body = parse_block();
            auto node = ASTNode::make(NodeKind::While);
            node->children.push_back(std::move(cond));
            node->children.push_back(std::move(body));
            return node;
        }
        // return
        if (cur().type == TK::KW_RETURN) {
            consume();
            auto node = ASTNode::make(NodeKind::Return);
            if (cur().type != TK::SEMI)
                node->children.push_back(parse_expr());
            expect(TK::SEMI,";");
            return node;
        }
        // int declaration
        if (cur().type == TK::KW_INT) {
            consume();
            std::string name = expect(TK::IDENT,"name").val;
            auto node = ASTNode::make(NodeKind::VarDecl, name);
            if (cur().type == TK::EQ) {
                consume();
                node->children.push_back(parse_expr());
            }
            expect(TK::SEMI,";");
            return node;
        }
        // expression statement
        auto e = parse_expr();
        expect(TK::SEMI,";");
        return e;
    }

    ASTPtr parse_expr() { return parse_assign(); }

    ASTPtr parse_assign() {
        auto lhs = parse_comparison();
        if (cur().type == TK::EQ) {
            consume();
            auto rhs = parse_assign();
            auto node = ASTNode::make(NodeKind::Assign);
            node->children.push_back(std::move(lhs));
            node->children.push_back(std::move(rhs));
            return node;
        }
        if (cur().type == TK::PLUSEQ) {
            consume();
            auto rhs = parse_assign();
            auto add = ASTNode::make(NodeKind::BinOp, "+");
            auto lhs_copy = ASTNode::make(NodeKind::Var, lhs->sval);
            add->children.push_back(std::move(lhs_copy));
            add->children.push_back(std::move(rhs));
            auto node = ASTNode::make(NodeKind::Assign);
            node->children.push_back(std::move(lhs));
            node->children.push_back(std::move(add));
            return node;
        }
        return lhs;
    }

    ASTPtr parse_comparison() {
        auto lhs = parse_additive();
        while (cur().type==TK::EQEQ||cur().type==TK::NEQ||
               cur().type==TK::LT  ||cur().type==TK::GT ||
               cur().type==TK::LEQ ||cur().type==TK::GEQ) {
            std::string op = consume().val;
            auto rhs = parse_additive();
            auto node = ASTNode::make(NodeKind::BinOp, op);
            node->children.push_back(std::move(lhs));
            node->children.push_back(std::move(rhs));
            lhs = std::move(node);
        }
        return lhs;
    }

    ASTPtr parse_additive() {
        auto lhs = parse_multiplicative();
        while (cur().type==TK::PLUS||cur().type==TK::MINUS) {
            std::string op = consume().val;
            auto rhs = parse_multiplicative();
            auto node = ASTNode::make(NodeKind::BinOp, op);
            node->children.push_back(std::move(lhs));
            node->children.push_back(std::move(rhs));
            lhs = std::move(node);
        }
        return lhs;
    }

    ASTPtr parse_multiplicative() {
        auto lhs = parse_unary();
        while (cur().type==TK::STAR||cur().type==TK::SLASH||cur().type==TK::PERCENT) {
            std::string op = consume().val;
            auto rhs = parse_unary();
            auto node = ASTNode::make(NodeKind::BinOp, op);
            node->children.push_back(std::move(lhs));
            node->children.push_back(std::move(rhs));
            lhs = std::move(node);
        }
        return lhs;
    }

    ASTPtr parse_unary() {
        if (cur().type==TK::MINUS) {
            consume();
            auto e = parse_primary();
            auto node = ASTNode::make(NodeKind::UnOp,"-");
            node->children.push_back(std::move(e));
            return node;
        }
        if (cur().type==TK::PLUSPLUS) {
            consume();
            auto e = parse_primary();
            auto node = ASTNode::make(NodeKind::PreInc);
            node->children.push_back(std::move(e));
            return node;
        }
        if (cur().type==TK::MINUSMINUS) {
            consume();
            auto e = parse_primary();
            auto node = ASTNode::make(NodeKind::PreDec);
            node->children.push_back(std::move(e));
            return node;
        }
        return parse_primary();
    }

    ASTPtr parse_primary() {
        if (cur().type == TK::INT_LIT) {
            int v = std::stoi(consume().val);
            return ASTNode::make(NodeKind::IntLit, "", v);
        }
        if (cur().type == TK::IDENT) {
            std::string name = consume().val;
            // Function call?
            if (cur().type == TK::LPAREN) {
                consume();
                auto node = ASTNode::make(NodeKind::Call, name);
                while (cur().type != TK::RPAREN) {
                    node->children.push_back(parse_expr());
                    if (cur().type == TK::COMMA) consume();
                }
                expect(TK::RPAREN, ")");
                return node;
            }
            return ASTNode::make(NodeKind::Var, name);
        }
        if (cur().type == TK::LPAREN) {
            consume();
            auto e = parse_expr();
            expect(TK::RPAREN, ")");
            return e;
        }
        throw std::runtime_error("Parse error: unexpected token '" + cur().val + "' line " + std::to_string(cur().line));
    }
};

// ────────────────────────────────────────────────────────────
//  CODE GENERATOR
//  Register allocation: simple — use R0 as accumulator,
//  R1 for temporaries, R2..R7 for spill.
//  Stack frame: local vars stored in stack.
// ────────────────────────────────────────────────────────────
class CodeGen {
public:
    std::string emit(ASTNode& prog) {
        out_ << "; Generated by ta-gcc\n";
        out_ << ".section .text\n\n";
        // Bootstrap: _start calls main then exits
        out_ << "_start:\n";
        out_ << "    CALL main\n";
        out_ << "    ; R0 = return value of main, use as exit code\n";
        out_ << "    MOV R1, R0\n";
        out_ << "    MOV R0, #0\n";   // SYS_EXIT
        out_ << "    INT 0x80\n";
        out_ << "    HLT\n\n";
        for (auto& fn : prog.children)
            emit_func(*fn);
        if (!data_section_.str().empty()) {
            out_ << "\n.section .data\n";
            out_ << data_section_.str();
        }
        return out_.str();
    }

private:
    std::ostringstream out_, data_section_;
    int label_counter_ = 0;

    // Per-function state
    std::map<std::string, int> locals_; // name → stack slot (negative from FP = SP)
    int  frame_size_    = 0;    // bytes
    std::string cur_func_;
    std::vector<std::string> params_;

    std::string new_label(const std::string& hint="L") {
        return "." + cur_func_ + "_" + hint + std::to_string(label_counter_++);
    }

    // Frame: SP grows down.
    // R13 = frame pointer (saved BP pattern)
    // Locals: [R13 - 8*slot]
    int alloc_local(const std::string& name) {
        ++frame_size_;
        locals_[name] = frame_size_;
        return frame_size_;
    }
    int slot_of(const std::string& name) {
        auto it = locals_.find(name);
        if (it == locals_.end()) throw std::runtime_error("Undeclared: " + name);
        return it->second;
    }

    void emit_func(ASTNode& fn) {
        // children: param Var nodes... then Block as last child
        locals_.clear();
        params_.clear();
        frame_size_ = 0;
        cur_func_ = fn.sval;

        // Collect params (all except last child)
        size_t block_idx = fn.children.size() - 1;
        for (size_t i = 0; i < block_idx; ++i)
            params_.push_back(fn.children[i]->sval);

        // Pre-allocate slots for params
        for (auto& p : params_) alloc_local(p);

        // Count all locals in the body to determine total frame size
        int body_locals = count_locals(*fn.children[block_idx]);
        int total_slots = (int)params_.size() + body_locals;

        out_ << fn.sval << ":\n";
        // Prologue: save FP, set FP = SP, allocate frame in ONE step
        out_ << "    PUSH R13\n";
        out_ << "    MOV R13, SP\n";
        if (total_slots > 0) {
            if (total_slots * 8 <= 255)
                out_ << "    MOV R15, #" << (total_slots * 8) << "\n";
            else
                out_ << "    MOVI R15, " << (total_slots * 8) << "\n";
            out_ << "    SUB SP, R15\n";
        }

        // Spill register arguments into their frame slots
        for (size_t i = 0; i < params_.size(); ++i) {
            int slot = slot_of(params_[i]);
            out_ << "    ; param " << params_[i] << " → [R13-" << (slot*8) << "]\n";
            out_ << "    STORE R" << i << ", [R13 + -" << (slot*8) << "]\n";
        }

        emit_block(*fn.children[block_idx]);

        // Epilogue
        out_ << ".ret_" << fn.sval << ":\n";
        out_ << "    MOV SP, R13\n";
        out_ << "    POP R13\n";
        if (fn.sval == "main") {
            out_ << "    RET\n";   // return to _start bootstrap
        } else {
            out_ << "    RET\n";
        }
        out_ << "\n";
    }

    int count_locals(ASTNode& block) {
        int n = 0;
        for (auto& s : block.children) {
            if (s->kind == NodeKind::VarDecl) ++n;
            if (s->kind == NodeKind::Block)   n += count_locals(*s);
            if (s->kind == NodeKind::If) {
                for (auto& c : s->children)
                    if (c->kind == NodeKind::Block) n += count_locals(*c);
            }
            if (s->kind == NodeKind::While) {
                if (!s->children.empty() && s->children.back()->kind == NodeKind::Block)
                    n += count_locals(*s->children.back());
            }
        }
        return n;
    }

    void emit_block(ASTNode& blk) {
        for (auto& s : blk.children)
            emit_stmt(*s);
    }

    void emit_stmt(ASTNode& n) {
        switch (n.kind) {
        case NodeKind::VarDecl: {
            int slot = alloc_local(n.sval);
            if (!n.children.empty()) {
                emit_expr(*n.children[0]); // result in R0
                out_ << "    STORE R0, [R13 + -" << (slot*8) << "]\n";
            }
            break;
        }
        case NodeKind::Assign: {
            emit_expr(*n.children[1]); // value in R0
            std::string name = n.children[0]->sval;
            int slot = slot_of(name);
            out_ << "    STORE R0, [R13 + -" << (slot*8) << "]\n";
            break;
        }
        case NodeKind::Return: {
            if (!n.children.empty())
                emit_expr(*n.children[0]);
            else
                out_ << "    MOV R0, #0\n";
            out_ << "    JMP .ret_" << cur_func_ << "\n";
            break;
        }
        case NodeKind::If: {
            std::string lbl_else = new_label("else");
            std::string lbl_end  = new_label("endif");
            emit_expr(*n.children[0]);   // condition in R0
            out_ << "    CMP R0, R0\n"; // just to set ZF
            out_ << "    MOVI R1, 0\n";
            out_ << "    CMP R0, R1\n"; // R0 == 0?
            out_ << "    JE " << lbl_else << "\n";
            emit_block(*n.children[1]);
            out_ << "    JMP " << lbl_end << "\n";
            out_ << lbl_else << ":\n";
            if (n.children.size() > 2) emit_block(*n.children[2]);
            out_ << lbl_end << ":\n";
            break;
        }
        case NodeKind::While: {
            std::string lbl_top  = new_label("wloop");
            std::string lbl_end  = new_label("wend");
            out_ << lbl_top << ":\n";
            emit_expr(*n.children[0]);  // result in R0; 0=false, non-zero=true
            out_ << "    MOVI R1, 0\n";
            out_ << "    CMP R0, R1\n"; // ZF set if R0==0 (false)
            out_ << "    JE " << lbl_end << "\n";
            emit_block(*n.children[1]);
            out_ << "    JMP " << lbl_top << "\n";
            out_ << lbl_end << ":\n";
            break;
        }
        default:
            emit_expr(n);
            break;
        }
    }

    // Emit expression; result always ends up in R0
    void emit_expr(ASTNode& n) {
        switch (n.kind) {
        case NodeKind::IntLit:
            if (n.ival >= 0 && n.ival <= 255)
                out_ << "    MOV R0, #" << n.ival << "\n";
            else
                out_ << "    MOVI R0, " << n.ival << "\n";
            break;
        case NodeKind::Var: {
            int slot = slot_of(n.sval);
            out_ << "    LOAD R0, [R13 + -" << (slot*8) << "]\n";
            break;
        }
        case NodeKind::BinOp: {
            // Evaluate LHS → R0, push; evaluate RHS → R0; pop to R1; operate
            emit_expr(*n.children[0]);
            out_ << "    PUSH R0\n";
            emit_expr(*n.children[1]);
            out_ << "    MOV R1, R0\n";
            out_ << "    POP R0\n";
            std::string op = n.sval;
            if (op=="+")  out_ << "    ADD R0, R1\n";
            else if (op=="-") out_ << "    SUB R0, R1\n";
            else if (op=="*") out_ << "    MUL R0, R1\n";
            else if (op=="/") out_ << "    DIV R0, R1\n";
            else if (op=="%") out_ << "    MOD R0, R1\n";
            else if (op=="==") {
                // CMP sets flags, then conditional jump to set result
                std::string lbl_true = new_label("eq_t");
                std::string lbl_end  = new_label("eq_e");
                out_ << "    CMP R0, R1\n";
                out_ << "    JE  " << lbl_true << "\n";
                out_ << "    MOV R0, #0\n";
                out_ << "    JMP " << lbl_end  << "\n";
                out_ << lbl_true << ":\n";
                out_ << "    MOV R0, #1\n";
                out_ << lbl_end  << ":\n";
            } else if (op=="!=") {
                std::string lbl_true = new_label("ne_t");
                std::string lbl_end  = new_label("ne_e");
                out_ << "    CMP R0, R1\n";
                out_ << "    JNE " << lbl_true << "\n";
                out_ << "    MOV R0, #0\n";
                out_ << "    JMP " << lbl_end  << "\n";
                out_ << lbl_true << ":\n";
                out_ << "    MOV R0, #1\n";
                out_ << lbl_end  << ":\n";
            } else if (op=="<") {
                std::string lbl_true = new_label("lt_t");
                std::string lbl_end  = new_label("lt_e");
                out_ << "    CMP R0, R1\n";
                out_ << "    JLT " << lbl_true << "\n";
                out_ << "    MOV R0, #0\n";
                out_ << "    JMP " << lbl_end  << "\n";
                out_ << lbl_true << ":\n";
                out_ << "    MOV R0, #1\n";
                out_ << lbl_end  << ":\n";
            } else if (op==">") {
                std::string lbl_true = new_label("gt_t");
                std::string lbl_end  = new_label("gt_e");
                out_ << "    CMP R0, R1\n";
                out_ << "    JGT " << lbl_true << "\n";
                out_ << "    MOV R0, #0\n";
                out_ << "    JMP " << lbl_end  << "\n";
                out_ << lbl_true << ":\n";
                out_ << "    MOV R0, #1\n";
                out_ << lbl_end  << ":\n";
            } else if (op=="<=") {
                std::string lbl_true = new_label("le_t");
                std::string lbl_end  = new_label("le_e");
                out_ << "    CMP R0, R1\n";
                out_ << "    JLE " << lbl_true << "\n";
                out_ << "    MOV R0, #0\n";
                out_ << "    JMP " << lbl_end  << "\n";
                out_ << lbl_true << ":\n";
                out_ << "    MOV R0, #1\n";
                out_ << lbl_end  << ":\n";
            } else if (op==">=") {
                std::string lbl_true = new_label("ge_t");
                std::string lbl_end  = new_label("ge_e");
                out_ << "    CMP R0, R1\n";
                out_ << "    JGE " << lbl_true << "\n";
                out_ << "    MOV R0, #0\n";
                out_ << "    JMP " << lbl_end  << "\n";
                out_ << lbl_true << ":\n";
                out_ << "    MOV R0, #1\n";
                out_ << lbl_end  << ":\n";
            }
            break;
        }
        case NodeKind::UnOp:
            emit_expr(*n.children[0]);
            if (n.sval=="-") {
                out_ << "    MOVI R1, 0\n";
                out_ << "    SUB R1, R0\n";
                out_ << "    MOV R0, R1\n";
            }
            break;
        case NodeKind::PreInc: {
            std::string name = n.children[0]->sval;
            int slot = slot_of(name);
            out_ << "    LOAD R0, [R13 + -" << (slot*8) << "]\n";
            out_ << "    INC R0\n";
            out_ << "    STORE R0, [R13 + -" << (slot*8) << "]\n";
            break;
        }
        case NodeKind::PreDec: {
            std::string name = n.children[0]->sval;
            int slot = slot_of(name);
            out_ << "    LOAD R0, [R13 + -" << (slot*8) << "]\n";
            out_ << "    DEC R0\n";
            out_ << "    STORE R0, [R13 + -" << (slot*8) << "]\n";
            break;
        }
        case NodeKind::Call: {
            // Evaluate each argument into R0, then move to Ri before call
            // To avoid clobbering, evaluate all args onto stack first,
            // then pop into registers in reverse order
            size_t nargs = n.children.size();
            for (size_t i = 0; i < nargs; ++i) {
                emit_expr(*n.children[i]);
                out_ << "    PUSH R0\n";   // save each evaluated arg
            }
            // Pop in reverse so R0=arg0, R1=arg1, ...
            for (int i = (int)nargs - 1; i >= 0; --i) {
                out_ << "    POP R" << i << "\n";
            }
            out_ << "    CALL " << n.sval << "\n";
            break;
        }
        default: break;
        }
    }
};

// ─── Main ───────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ta-gcc <source.c> [-o output.asm]\n";
        return 1;
    }
    std::string src_path = argv[1];
    std::string out_path = (argc >= 4 && std::string(argv[2])=="-o") ? argv[3]
                         : src_path.substr(0, src_path.rfind('.')) + ".asm";

    std::ifstream f(src_path);
    if (!f) { std::cerr << "Cannot open: " << src_path << "\n"; return 1; }
    std::string src((std::istreambuf_iterator<char>(f)), {});

    try {
        Lexer lex(src);
        auto toks = lex.tokenise();

        Parser parser(std::move(toks));
        auto ast = parser.parse_program();

        CodeGen cg;
        std::string asm_src = cg.emit(*ast);

        std::ofstream out(out_path);
        if (!out) { std::cerr << "Cannot write: " << out_path << "\n"; return 1; }
        out << asm_src;
        std::cout << "[ta-gcc] " << src_path << " → " << out_path << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[ta-gcc] " << e.what() << "\n";
        return 1;
    }
}
