#include "SymbolTable.h"

SymbolTable::SymbolTable() {
    // 最外层是全局作用域
    scopes_.emplace_back();
}

void SymbolTable::pushScope() {
    scopes_.emplace_back();
}

void SymbolTable::popScope() {
    // 至少保留全局作用域
    if (scopes_.size() > 1) {
        scopes_.pop_back();
    }
}

Symbol* SymbolTable::define(const std::string& name, SymbolKind kind) {
    auto& current = scopes_.back();
    auto it = current.find(name);
    if (it != current.end()) {
        return it->second;
    }
    arena_.push_back(std::make_unique<Symbol>());
    Symbol* symbol = arena_.back().get();
    symbol->name = name;
    symbol->kind = kind;
    current.emplace(name, symbol);
    return symbol;
}

Symbol* SymbolTable::newTemporary(SymbolKind kind) {
    std::string name = "T" + std::to_string(nextTempIndex_++);
    return define(name, kind);
}

Symbol* SymbolTable::lookup(const std::string& name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        auto it = scope->find(name);
        if (it != scope->end()) {
            return it->second;
        }
    }
    return nullptr;
}
