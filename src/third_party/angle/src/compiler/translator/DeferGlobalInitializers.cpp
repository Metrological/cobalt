//
// Copyright (c) 2016 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// DeferGlobalInitializers is an AST traverser that moves global initializers into a function, and
// adds a function call to that function in the beginning of main().
// This enables initialization of globals with uniforms or non-constant globals, as allowed by
// the WebGL spec. Some initializers referencing non-constants may need to be unfolded into if
// statements in HLSL - this kind of steps should be done after DeferGlobalInitializers is run.
//

#include "compiler/translator/DeferGlobalInitializers.h"

#include "compiler/translator/FindMain.h"
#include "compiler/translator/IntermNode.h"
#include "compiler/translator/SymbolTable.h"

namespace sh
{

namespace
{

void GetDeferredInitializers(TIntermDeclaration *declaration,
                             TIntermSequence *deferredInitializersOut)
{
    // We iterate with an index instead of using an iterator since we're replacing the children of
    // declaration inside the loop.
    for (size_t i = 0; i < declaration->getSequence()->size(); ++i)
    {
        TIntermNode *declarator = declaration->getSequence()->at(i);
        TIntermBinary *init     = declarator->getAsBinaryNode();
        if (init)
        {
            TIntermSymbol *symbolNode = init->getLeft()->getAsSymbolNode();
            ASSERT(symbolNode);
            TIntermTyped *expression = init->getRight();

            if ((expression->getQualifier() != EvqConst ||
                 (expression->getAsConstantUnion() == nullptr &&
                  !expression->isConstructorWithOnlyConstantUnionParameters())))
            {
                // For variables which are not constant, defer their real initialization until
                // after we initialize uniforms.
                // Deferral is done also in any cases where the variable has not been constant
                // folded, since otherwise there's a chance that HLSL output will generate extra
                // statements from the initializer expression.
                TIntermBinary *deferredInit =
                    new TIntermBinary(EOpAssign, symbolNode->deepCopy(), init->getRight());
                deferredInitializersOut->push_back(deferredInit);

                // Change const global to a regular global if its initialization is deferred.
                // This can happen if ANGLE has not been able to fold the constant expression used
                // as an initializer.
                ASSERT(symbolNode->getQualifier() == EvqConst ||
                       symbolNode->getQualifier() == EvqGlobal);
                if (symbolNode->getQualifier() == EvqConst)
                {
                    // All of the siblings in the same declaration need to have consistent
                    // qualifiers.
                    auto *siblings = declaration->getSequence();
                    for (TIntermNode *siblingNode : *siblings)
                    {
                        TIntermBinary *siblingBinary = siblingNode->getAsBinaryNode();
                        if (siblingBinary)
                        {
                            ASSERT(siblingBinary->getOp() == EOpInitialize);
                            siblingBinary->getLeft()->getTypePointer()->setQualifier(EvqGlobal);
                        }
                        siblingNode->getAsTyped()->getTypePointer()->setQualifier(EvqGlobal);
                    }
                    // This node is one of the siblings.
                    ASSERT(symbolNode->getQualifier() == EvqGlobal);
                }
                // Remove the initializer from the global scope and just declare the global instead.
                declaration->replaceChildNode(init, symbolNode);
            }
        }
    }
}

void InsertInitFunction(TIntermBlock *root, TIntermSequence *deferredInitializers)
{
    TSymbolUniqueId initFunctionId;
    const char *functionName = "initializeGlobals";

    // Add function prototype to the beginning of the shader
    TIntermFunctionPrototype *functionPrototypeNode =
        TIntermTraverser::CreateInternalFunctionPrototypeNode(TType(EbtVoid), functionName,
                                                              initFunctionId);
    root->getSequence()->insert(root->getSequence()->begin(), functionPrototypeNode);

    // Add function definition to the end of the shader
    TIntermBlock *functionBodyNode = new TIntermBlock();
    functionBodyNode->getSequence()->swap(*deferredInitializers);
    TIntermFunctionDefinition *functionDefinition =
        TIntermTraverser::CreateInternalFunctionDefinitionNode(TType(EbtVoid), functionName,
                                                               functionBodyNode, initFunctionId);
    root->getSequence()->push_back(functionDefinition);

    // Insert call into main function
    TIntermFunctionDefinition *main = FindMain(root);
    ASSERT(main != nullptr);
    TIntermAggregate *functionCallNode = TIntermTraverser::CreateInternalFunctionCallNode(
        TType(EbtVoid), functionName, initFunctionId, nullptr);

    TIntermBlock *mainBody = main->getBody();
    ASSERT(mainBody != nullptr);
    mainBody->getSequence()->insert(mainBody->getSequence()->begin(), functionCallNode);
}

}  // namespace

void DeferGlobalInitializers(TIntermBlock *root)
{
    TIntermSequence *deferredInitializers = new TIntermSequence();

    // Loop over all global statements and process the declarations. This is simpler than using a
    // traverser.
    for (TIntermNode *declaration : *root->getSequence())
    {
        TIntermDeclaration *asVariableDeclaration = declaration->getAsDeclarationNode();
        if (asVariableDeclaration)
        {
            GetDeferredInitializers(asVariableDeclaration, deferredInitializers);
        }
    }

    // Add the function with initialization and the call to that.
    if (!deferredInitializers->empty())
    {
        InsertInitFunction(root, deferredInitializers);
    }
}

}  // namespace sh
