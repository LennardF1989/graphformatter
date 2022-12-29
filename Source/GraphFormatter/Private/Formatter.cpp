/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Howaajin. All rights reserved.
 *  Licensed under the MIT License. See License in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

#include "Formatter.h"
#include "FormatterCommands.h"
#include "FormatterGraph.h"
#include "FormatterSettings.h"
#include "FormatterLog.h"

#include "BehaviorTree/BehaviorTree.h"
#include "EdGraphNode_Comment.h"
#include "Math/Ray.h"
#include "SGraphNodeComment.h"
#include "SGraphPanel.h"

void FFormatter::SetCurrentEditor(SGraphEditor* Editor, UObject* Object)
{
    CurrentEditor = Editor;
    IsVerticalLayout = false;
    IsBehaviorTree = false;
    IsBlueprint = false;
    if (Cast<UBehaviorTree>(Object))
    {
        IsVerticalLayout = true;
        IsBehaviorTree = true;
    }
    if (Cast<UBlueprint>(Object))
    {
        IsBlueprint = true;
    }
}

bool FFormatter::IsAssetSupported(const UObject* Object) const
{
    const UFormatterSettings* Settings = GetDefault<UFormatterSettings>();
    if (const bool* Enabled = Settings->SupportedAssetTypes.Find(Object->GetClass()->GetName()))
    {
        return Enabled != nullptr && *Enabled;
    }
    return false;
}

/** Matches widgets by type */
struct FWidgetTypeMatcher
{
    FWidgetTypeMatcher(const FName& InType)
        : TypeName(InType)
    {}

    bool IsMatch(const TSharedRef<const SWidget>& InWidget) const
    {
        return TypeName == InWidget->GetType();
    }

    const FName& TypeName;
};

SGraphEditor* FFormatter::FindGraphEditorForTopLevelWindow() const
{
    FSlateApplication& Application = FSlateApplication::Get();
    auto ActiveWindow = Application.GetActiveTopLevelWindow();
    if (!ActiveWindow.IsValid())
    {
        return nullptr;
    }
    FGeometry InnerWindowGeometry = ActiveWindow->GetWindowGeometryInWindow();
    FArrangedChildren JustWindow(EVisibility::Visible);
    JustWindow.AddWidget(FArrangedWidget(ActiveWindow.ToSharedRef(), InnerWindowGeometry));

    FWidgetPath WidgetPath(ActiveWindow.ToSharedRef(), JustWindow);
    if (WidgetPath.ExtendPathTo(FWidgetTypeMatcher("SGraphEditor"), EVisibility::Visible))
    {
        return StaticCast<SGraphEditor*>(&WidgetPath.GetLastWidget().Get());
    }
    return nullptr;
}

SGraphEditor* FFormatter::FindGraphEditorByCursor() const
{
    FSlateApplication& Application = FSlateApplication::Get();
    FWidgetPath WidgetPath = Application.LocateWindowUnderMouse(Application.GetCursorPos(), Application.GetInteractiveTopLevelWindows());
    for (int i = WidgetPath.Widgets.Num() - 1; i >= 0; i--)
    {
        if (WidgetPath.Widgets[i].Widget->GetTypeAsString() == "SGraphEditor")
        {
            return StaticCast<SGraphEditor*>(&WidgetPath.Widgets[i].Widget.Get());
        }
    }
    return nullptr;
}

SGraphPanel* FFormatter::GetCurrentPanel() const
{
    return CurrentEditor->GetGraphPanel();
}

SGraphNode* FFormatter::GetWidget(const UEdGraphNode* Node) const
{
    SGraphPanel* GraphPanel = GetCurrentPanel();
    if (GraphPanel != nullptr)
    {
        TSharedPtr<SGraphNode> NodeWidget = GraphPanel->GetNodeWidgetFromGuid(Node->NodeGuid);
        return NodeWidget.Get();
    }
    return nullptr;
}

TSet<UEdGraphNode*> FFormatter::GetAllNodes() const
{
    TSet<UEdGraphNode*> Nodes;
    if (CurrentEditor)
    {
        for (UEdGraphNode* Node : CurrentEditor->GetCurrentGraph()->Nodes)
        {
            Nodes.Add(Node);
        }
    }
    return Nodes;
}

float FFormatter::GetCommentNodeTitleHeight(const UEdGraphNode* Node) const
{
    /** Titlebar Offset - taken from SGraphNodeComment.cpp */
    static const FSlateRect TitleBarOffset(13, 8, -3, 0);

    SGraphNode* CommentNode = GetWidget(Node);
    if (CommentNode)
    {
        SGraphNodeComment* NodeWidget = StaticCast<SGraphNodeComment*>(CommentNode);
        FSlateRect Rect = NodeWidget->GetTitleRect();
        return Rect.GetSize().Y + TitleBarOffset.Top;
    }
    return 0;
}

FVector2D FFormatter::GetNodeSize(const UEdGraphNode* Node) const
{
    auto GraphNode = GetWidget(Node);
    if (GraphNode != nullptr)
    {
        FVector2D Size = GraphNode->GetDesiredSize();
        return Size;
    }
    return FVector2D(Node->NodeWidth, Node->NodeHeight);
}

FVector2D FFormatter::GetNodePosition(const UEdGraphNode* Node) const
{
    auto GraphNode = GetWidget(Node);
    if (GraphNode != nullptr)
    {
        return GraphNode->GetPosition();
    }
    return FVector2D();
}

FVector2D FFormatter::GetPinOffset(const UEdGraphPin* Pin) const
{
    auto GraphNode = GetWidget(Pin->GetOwningNodeUnchecked());
    if (GraphNode != nullptr)
    {
        auto PinWidget = GraphNode->FindWidgetForPin(const_cast<UEdGraphPin*>(Pin));
        if (PinWidget.IsValid())
        {
            auto Offset = PinWidget->GetNodeOffset();
            return Offset;
        }
    }
    return FVector2D::ZeroVector;
}

FSlateRect FFormatter::GetNodesBound(const TSet<UEdGraphNode*> Nodes) const
{
    FSlateRect Bound;
    for (auto Node : Nodes)
    {
        FVector2D Pos = GetNodePosition(Node);
        FVector2D Size = GetNodeSize(Node);
        FSlateRect NodeBound = FSlateRect::FromPointAndExtent(Pos, Size);
        Bound = Bound.IsValid() ? Bound.Expand(NodeBound) : NodeBound;
    }
    return Bound;
}

bool FFormatter::IsExecPin(const UEdGraphPin* Pin) const
{
    return Pin->PinType.PinCategory == "Exec";
}

void FFormatter::Translate(TSet<UEdGraphNode*> Nodes, FVector2D Offset) const
{
    UEdGraph* Graph = CurrentEditor->GetCurrentGraph();
    if (!Graph || !CurrentEditor)
    {
        return;
    }
    if (Offset.X == 0 && Offset.Y == 0)
    {
        return;
    }
    for (auto Node : Nodes)
    {
        auto WidgetNode = GetWidget(Node);
        SGraphPanel::SNode::FNodeSet Filter;
        WidgetNode->MoveTo(WidgetNode->GetPosition() + Offset, Filter, true);
    }
}

static TSet<UEdGraphNode*> GetSelectedNodes(SGraphEditor* GraphEditor)
{
    TSet<UEdGraphNode*> SelectedGraphNodes;
    TSet<UObject*> SelectedNodes = GraphEditor->GetSelectedNodes();
    for (UObject* Node : SelectedNodes)
    {
        UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Node);
        if (GraphNode)
        {
            SelectedGraphNodes.Add(GraphNode);
        }
    }
    return SelectedGraphNodes;
}

static bool IsNodeUnderRect( const TSharedRef<SGraphNode> InNodeWidget, const FSlateRect& Rect) 
{
	const FVector2D NodePosition = Rect.GetTopLeft();
	const FVector2D NodeSize = Rect.GetSize();
	const FSlateRect CommentRect(NodePosition.X, NodePosition.Y, NodePosition.X + NodeSize.X, NodePosition.Y + NodeSize.Y);

	const FVector2D InNodePosition = InNodeWidget->GetPosition();
	const FVector2D InNodeSize = InNodeWidget->GetDesiredSize();

	const FSlateRect NodeGeometryGraphSpace(InNodePosition.X, InNodePosition.Y, InNodePosition.X + InNodeSize.X, InNodePosition.Y + InNodeSize.Y);
	return FSlateRect::IsRectangleContained(CommentRect, NodeGeometryGraphSpace);
}

TSet<UEdGraphNode*> FFormatter::GetNodesUnderComment(const UEdGraphNode_Comment* CommentNode) const
{
    SGraphNode* CommentNodeWidget = GetWidget(CommentNode);
    auto CommentSize = CommentNodeWidget->GetDesiredSize();
    if (CommentSize.IsZero())
    {
        return TSet<UEdGraphNode*>();
    }
    TSet<UEdGraphNode*> Result;
    SGraphPanel* Panel = GetCurrentPanel();
    FChildren* PanelChildren = Panel->GetAllChildren();
    int32 NumChildren = PanelChildren->Num();
    FVector2D CommentNodePosition = CommentNodeWidget->GetPosition();
    FSlateRect CommentRect = FSlateRect(CommentNodePosition, CommentNodePosition + CommentSize);
    for (int32 NodeIndex = 0; NodeIndex < NumChildren; ++NodeIndex)
    {
        const TSharedRef<SGraphNode> SomeNodeWidget = StaticCastSharedRef<SGraphNode>(PanelChildren->GetChildAt(NodeIndex));
        UObject* GraphObject = SomeNodeWidget->GetObjectBeingDisplayed();
        if (GraphObject != CommentNode)
        {
            if (IsNodeUnderRect(SomeNodeWidget, CommentRect))
            {
                Result.Add(Cast<UEdGraphNode>(GraphObject));
            }
        }
    }
    return Result;
}

static TSet<UEdGraphNode*> DoSelectionStrategy(UEdGraph* Graph, TSet<UEdGraphNode*> Selected)
{
    if (Selected.Num() != 0)
    {
        TSet<UEdGraphNode*> SelectedGraphNodes;
        for (UEdGraphNode* GraphNode : Selected)
        {
            SelectedGraphNodes.Add(GraphNode);
            if (GraphNode->IsA(UEdGraphNode_Comment::StaticClass()))
            {
                UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(GraphNode);
                auto NodesInComment = FFormatter::Instance().GetNodesUnderComment(CommentNode);
                for (UObject* ObjectInComment : NodesInComment)
                {
                    UEdGraphNode* NodeInComment = Cast<UEdGraphNode>(ObjectInComment);
                    SelectedGraphNodes.Add(NodeInComment);
                }
            }
        }
        return SelectedGraphNodes;
    }
    TSet<UEdGraphNode*> SelectedGraphNodes;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        SelectedGraphNodes.Add(Node);
    }
    return SelectedGraphNodes;
}

void FFormatter::Format() const
{
    UEdGraph* Graph = CurrentEditor->GetCurrentGraph();
    if (!Graph || !CurrentEditor)
    {
        return;
    }
    auto SelectedNodes = GetSelectedNodes(CurrentEditor);
    SelectedNodes = DoSelectionStrategy(Graph, SelectedNodes);
    FFormatterGraph FormatterGraph(SelectedNodes);
    FormatterGraph.Format();
    auto BoundMap = FormatterGraph.GetBoundMap();
    const FScopedTransaction Transaction(FFormatterCommands::Get().FormatGraph->GetLabel());
    for (auto NodeRectPair : BoundMap)
    {
        NodeRectPair.Key->Modify();
        if (NodeRectPair.Key->IsA(UEdGraphNode_Comment::StaticClass()))
        {
            auto CommentNode = Cast<UEdGraphNode_Comment>(NodeRectPair.Key);
            CommentNode->SetBounds(NodeRectPair.Value);
        }
        else
        {
            auto WidgetNode = GetWidget(NodeRectPair.Key);
            SGraphPanel::SNode::FNodeSet Filter;
            WidgetNode->MoveTo(NodeRectPair.Value.GetTopLeft(), Filter, true);
        }
    }
    
    Graph->NotifyGraphChanged();
}

void FFormatter::PlaceBlock() const
{
    UEdGraph* Graph = CurrentEditor->GetCurrentGraph();
    if (!Graph || !CurrentEditor)
    {
        return;
    }
    auto SelectedNodes = GetSelectedNodes(CurrentEditor);
    auto ConnectedNodesLeft = FFormatterGraph::GetNodesConnected(SelectedNodes, FFormatterGraph::EInOutOption::EIOO_IN);
    FVector2D ConnectCenter;
    const UFormatterSettings& Settings = *GetDefault<UFormatterSettings>();
    const FScopedTransaction Transaction(FFormatterCommands::Get().PlaceBlock->GetLabel());
    if (FFormatterGraph::GetNodesConnectCenter(SelectedNodes, ConnectCenter, FFormatterGraph::EInOutOption::EIOO_IN))
    {
        auto Center = FVector(ConnectCenter.X, ConnectCenter.Y, 0);
        auto Direction = IsVerticalLayout ? FVector(0, 1, 0) : FVector(1, 0, 0);
        auto RightRay = FRay(Center, Direction, true);
        FSlateRect Bound = GetNodesBound(ConnectedNodesLeft);
        auto RightBound = IsVerticalLayout ? FVector(0, Bound.Bottom, 0) : FVector(Bound.Right, 0, 0);
        auto LinkedCenter3D = RightRay.PointAt(RightRay.GetParameter(RightBound));
        auto LinkedCenterTo = FVector2D(LinkedCenter3D) + (IsVerticalLayout ? FVector2D(0, Settings.HorizontalSpacing) : FVector2D(Settings.HorizontalSpacing, 0));
        FFormatterGraph::GetNodesConnectCenter(SelectedNodes, ConnectCenter, FFormatterGraph::EInOutOption::EIOO_IN, true);
        Center = FVector(ConnectCenter.X, ConnectCenter.Y, 0);
        Direction = IsVerticalLayout ? FVector(0, -1, 0) : FVector(-1, 0, 0);
        auto LeftRay = FRay(Center, Direction, true);
        Bound = GetNodesBound(SelectedNodes);
        auto LeftBound = IsVerticalLayout ? FVector(0, Bound.Top, 0) : FVector(Bound.Left, 0, 0);
        LinkedCenter3D = LeftRay.PointAt(LeftRay.GetParameter(LeftBound));
        auto LinkedCenterFrom = FVector2D(LinkedCenter3D);
        FVector2D Offset = LinkedCenterTo - LinkedCenterFrom;
        Translate(SelectedNodes, Offset);
    }
    auto ConnectedNodesRight = FFormatterGraph::GetNodesConnected(SelectedNodes, FFormatterGraph::EInOutOption::EIOO_OUT);
    if (FFormatterGraph::GetNodesConnectCenter(SelectedNodes, ConnectCenter, FFormatterGraph::EInOutOption::EIOO_OUT))
    {
        auto Center = FVector(ConnectCenter.X, ConnectCenter.Y, 0);
        auto Direction = IsVerticalLayout ? FVector(0, -1, 0) : FVector(-1, 0, 0);
        auto LeftRay = FRay(Center, Direction, true);
        FSlateRect Bound = GetNodesBound(ConnectedNodesRight);
        auto LeftBound = IsVerticalLayout ? FVector(0, Bound.Top, 0) : FVector(Bound.Left, 0, 0);
        auto LinkedCenter3D = LeftRay.PointAt(LeftRay.GetParameter(LeftBound));
        auto LinkedCenterTo = FVector2D(LinkedCenter3D) - (IsVerticalLayout ? FVector2D(0, Settings.HorizontalSpacing) : FVector2D(Settings.HorizontalSpacing, 0));
        FFormatterGraph::GetNodesConnectCenter(SelectedNodes, ConnectCenter, FFormatterGraph::EInOutOption::EIOO_OUT, true);
        Center = FVector(ConnectCenter.X, ConnectCenter.Y, 0);
        Direction = IsVerticalLayout ? FVector(0, 1, 0) : FVector(1, 0, 0);
        auto RightRay = FRay(Center, Direction, true);
        Bound = GetNodesBound(SelectedNodes);
        auto RightBound = IsVerticalLayout ? FVector(0, Bound.Bottom, 0) : FVector(Bound.Right, 0, 0);
        LinkedCenter3D = RightRay.PointAt(RightRay.GetParameter(RightBound));
        auto LinkedCenterFrom = FVector2D(LinkedCenter3D);
        FVector2D Offset = LinkedCenterFrom - LinkedCenterTo;
        Translate(ConnectedNodesRight, Offset);
    }
    Graph->NotifyGraphChanged();
}
