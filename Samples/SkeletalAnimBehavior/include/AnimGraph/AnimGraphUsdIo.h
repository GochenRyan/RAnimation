#pragma once
#include <string>

#include <AnimGraph/AnimGraphAsset.h>

// .animgraph.usda read/write. pxr-free header; the implementation is the sample's third USD TU.
//
//   #usda 1.0
//   ( defaultPrim = "AnimGraph"
//     customLayerData = { dictionary ranim = { int animGraphVersion = 1  string doc = "..." } } )
//   def Scope "AnimGraph" { custom int ranim:outputNode = 3
//     def Scope "Node_1" { custom string ranim:type = "ClipPlayer"  custom int ranim:clipIndex = 9
//                          custom float ranim:playRate = 1  custom bool ranim:looping = 1
//                          custom float2 ranim:editorPos = (120, 80) }
//     def Scope "Node_3" { custom string ranim:type = "TwoWayBlend"  custom float ranim:param = 0.5
//                          custom rel ranim:in:A = </AnimGraph/Node_1>  custom rel ranim:in:B = </AnimGraph/Node_2> }
//   }
//
// Node ids are the Node_<i> suffixes (dense, 0-based); links live on the consumer as ranim:in:<Pin>
// relationships (every node has exactly one pose output). CpuOnly markers are stored as real nodes.

namespace RAnimation
{
    constexpr int kAnimGraphFormatVersion = 1;

    bool SaveAnimGraphUsd(const GraphDesc& editorDesc,
                          const GraphLayout& layout,
                          const std::string& doc,
                          const std::string& path,
                          std::string& outError);

    bool LoadAnimGraphUsd(const std::string& path,
                          GraphDesc& outEditorDesc,
                          GraphLayout& outLayout,
                          std::string& outDoc,
                          std::string& outError);
} // namespace RAnimation
