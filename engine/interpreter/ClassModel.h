#pragma once

#include "../common/SourceText.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cuff
{

    struct FunctionDecl;
    struct FieldDecl;

    // What the interpreter knows about a class once it is declared: its parent, its fields in layout order (the
    // parent's first) and its methods, with inherited ones already merged in. Objects point at it, so a class
    // outlives the interpreter that declared it for as long as any of its objects is alive.
    struct ClassInfo
    {
        struct Field
        {
            std::string name;
            uint32_t nameId = 0;
            bool isPrivate = false;
            bool isConstant = false;
            const ClassInfo *declaredIn = nullptr;
            const FieldDecl *decl = nullptr;
            uint8_t typeKind = 0; // the TypeKind of the declared type, beside the flags a write checks
        };

        struct Method
        {
            const FunctionDecl *decl = nullptr;
            const ClassInfo *owner = nullptr; // the class that wrote it: where `super` and `private` start from
        };

        std::string name;
        uint32_t nameId = 0;
        std::shared_ptr<const ClassInfo> parent;
        bool isBuiltin = false;
        int depth = 0; // 0 for a class without a parent
        std::vector<Field> fields;
        std::unordered_map<uint32_t, uint32_t> fieldIndex;
        std::unordered_map<uint32_t, Method> methods;
        const Method *init = nullptr;  // the nearest `init`, or null
        const Method *toStr = nullptr; // the nearest `to_str`, or null
        // A number no other class of the same interpreter has, so that what was looked up for one class (where a
        // field sits, which function a method name runs) can be kept beside the instruction that asked and tested
        // with one comparison. An address would not do: a class that is declared again may be given the address of
        // the one it replaced.
        uint32_t serial = 0;
        SourceRef source; // the text the class was written in: where an error in a field's default value is drawn from
        // What the interpreter that declared the class keeps for it: how each field gets its first value.
        mutable std::shared_ptr<void> runtime;

        ClassInfo() = default;
        ClassInfo(const ClassInfo &) = delete;
        ClassInfo &operator=(const ClassInfo &) = delete;

        const Field *findField(uint32_t id) const
        {
            auto it = fieldIndex.find(id);
            return it == fieldIndex.end() ? nullptr : &fields[it->second];
        }

        const Method *findMethod(uint32_t id) const
        {
            auto it = methods.find(id);
            return it == methods.end() ? nullptr : &it->second;
        }

        bool isSubclassOf(const ClassInfo *other) const
        {
            for (const ClassInfo *c = this; c; c = c->parent.get())
                if (c == other)
                    return true;
            return false;
        }
    };

}
