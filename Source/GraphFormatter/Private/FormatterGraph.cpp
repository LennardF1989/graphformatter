/*---------------------------------------------------------------------------------------------
 *  Copyright (c) Howaajin. All rights reserved.
 *  Licensed under the MIT License. See License in the project root for license information.
 *--------------------------------------------------------------------------------------------*/

#include "FormatterGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "FormatterSettings.h"
#include "IPositioningStrategy.h"
#include "EvenlyPlaceStrategy.h"
#include "PriorityPositioningStrategy.h"

bool FFormatterEdge::IsCrossing(const FFormatterEdge* Edge) const
{
	return FromIndex < Edge->FromIndex && ToIndex > Edge->ToIndex || FromIndex > Edge->FromIndex && ToIndex < Edge->ToIndex;
}

FFormatterNode::FFormatterNode(UEdGraphNode* InNode)
	: Guid(InNode->NodeGuid)
	  , OriginalNode(InNode)
	  , SubGraph(nullptr)
	  , Size(FVector2D())
	  , PathDepth(0)
	  , Position(FVector2D(InNode->NodePosX, InNode->NodePosY))
{
	for (auto Pin : InNode->Pins)
	{
		auto NewPin = new FFormatterPin;
		NewPin->Guid = FGuid::NewGuid();
		NewPin->OriginalPin = Pin;
		NewPin->Direction = Pin->Direction;
		NewPin->OwningNode = this;
		if (Pin->Direction == EGPD_Input)
		{
			InPins.Add(NewPin);
		}
		else
		{
			OutPins.Add(NewPin);
		}
	}
}

FFormatterNode::FFormatterNode(const FFormatterNode& Other)
	: Guid(Other.Guid)
	  , OriginalNode(Other.OriginalNode)
	  , Size(Other.Size)
	  , PathDepth(Other.PathDepth)
	  , Position(Other.Position)
{
	if (Other.SubGraph != nullptr)
	{
		SubGraph = new FFormatterGraph(*Other.SubGraph);
	}
	else
	{
		SubGraph = nullptr;
	}
	for (auto Pin : Other.InPins)
	{
		auto NewPin = new FFormatterPin;
		NewPin->Guid = Pin->Guid;
		NewPin->OriginalPin = Pin->OriginalPin;
		NewPin->Direction = Pin->Direction;
		NewPin->OwningNode = this;
		NewPin->NodeOffset = Pin->NodeOffset;
		InPins.Add(NewPin);
	}
	for (auto Pin : Other.OutPins)
	{
		auto NewPin = new FFormatterPin;
		NewPin->Guid = Pin->Guid;
		NewPin->OriginalPin = Pin->OriginalPin;
		NewPin->Direction = Pin->Direction;
		NewPin->OwningNode = this;
		NewPin->NodeOffset = Pin->NodeOffset;
		OutPins.Add(NewPin);
	}
}

FFormatterNode::FFormatterNode()
	: Guid(FGuid::NewGuid())
	  , OriginalNode(nullptr)
	  , SubGraph(nullptr)
	  , Size(FVector2D(1, 1))
	  , PathDepth(0)
	  , PositioningPriority(INT_MAX)
	  , Position(FVector2D::ZeroVector)
{
	auto InPin = new FFormatterPin;
	InPin->Guid = FGuid::NewGuid();
	InPin->OriginalPin = nullptr;
	InPin->Direction = EGPD_Input;
	InPin->OwningNode = this;
	InPin->NodeOffset = FVector2D::ZeroVector;

	auto OutPin = new FFormatterPin;
	OutPin->Guid = FGuid::NewGuid();
	OutPin->OriginalPin = nullptr;
	OutPin->Direction = EGPD_Output;
	OutPin->OwningNode = this;
	OutPin->NodeOffset = FVector2D::ZeroVector;

	InPins.Add(InPin);
	OutPins.Add(OutPin);
}

void FFormatterNode::Connect(FFormatterPin* SourcePin, FFormatterPin* TargetPin)
{
	const auto Edge = new FFormatterEdge;
	Edge->From = SourcePin;
	Edge->To = TargetPin;
	if (SourcePin->Direction == EGPD_Output)
	{
		OutEdges.Add(Edge);
	}
	else
	{
		InEdges.Add(Edge);
	}
}

void FFormatterNode::Disconnect(FFormatterPin* SourcePin, FFormatterPin* TargetPin)
{
	const auto Predicate = [SourcePin, TargetPin](const FFormatterEdge* Edge)
	{
		return Edge->From == SourcePin && Edge->To == TargetPin;
	};
	if (SourcePin->Direction == EGPD_Output)
	{
		const auto Index = OutEdges.IndexOfByPredicate(Predicate);
		if (Index != INDEX_NONE)
		{
			OutEdges.RemoveAt(Index);
		}
	}
	else
	{
		const auto Index = InEdges.IndexOfByPredicate(Predicate);
		if (Index != INDEX_NONE)
		{
			InEdges.RemoveAt(Index);
		}
	}
}

TArray<FFormatterNode*> FFormatterNode::GetSuccessors() const
{
	TArray<FFormatterNode*> Result;
	for (auto Edge : OutEdges)
	{
		Result.Add(Edge->To->OwningNode);
	}
	return Result;
}

TArray<FFormatterNode*> FFormatterNode::GetPredecessors() const
{
	TArray<FFormatterNode*> Result;
	for (auto Edge : InEdges)
	{
		Result.Add(Edge->To->OwningNode);
	}
	return Result;
}

bool FFormatterNode::IsSource() const
{
	return InEdges.Num() == 0;
}

bool FFormatterNode::IsSink() const
{
	return OutEdges.Num() == 0;
}

bool FFormatterNode::AnySuccessorPathDepthEqu0() const
{
	for (auto OutEdge : OutEdges)
	{
		if (OutEdge->To->OwningNode->PathDepth == 0)
		{
			return true;
		}
	}
	return false;
}

int32 FFormatterNode::GetInputPinCount() const
{
	return InPins.Num();
}

int32 FFormatterNode::GetInputPinIndex(FFormatterPin* InputPin) const
{
	return InPins.Find(InputPin);
}

int32 FFormatterNode::GetOutputPinCount() const
{
	return OutPins.Num();
}

int32 FFormatterNode::GetOutputPinIndex(FFormatterPin* OutputPin) const
{
	return OutPins.Find(OutputPin);
}

TArray<FFormatterEdge*> FFormatterNode::GetEdgeLinkedToLayer(const TArray<FFormatterNode*>& Layer, int32 StartIndex, EEdGraphPinDirection Direction) const
{
	TArray<FFormatterEdge*> Result;
	const TArray<FFormatterEdge*>& Edges = Direction == EGPD_Output ? OutEdges : InEdges;
	for (auto Edge : Edges)
	{
		int32 Index = 0;
		for (auto NextLayerNode : Layer)
		{
			if (Edge->To->OwningNode != NextLayerNode)
			{
				Index += Direction == EGPD_Output ? NextLayerNode->GetInputPinCount() : NextLayerNode->GetOutputPinCount();
			}
			else
			{
				Index += Direction == EGPD_Output ? NextLayerNode->GetInputPinIndex(Edge->To) : NextLayerNode->GetOutputPinIndex(Edge->To);
				Edge->FromIndex = StartIndex + (Direction == EGPD_Output ? GetOutputPinIndex(Edge->From) : GetInputPinIndex(Edge->From));
				Edge->ToIndex = Index;
				Result.Add(Edge);
			}
		}
	}
	return Result;
}

float FFormatterNode::CalcBarycenter(const TArray<FFormatterNode*>& Layer, int32 StartIndex, EEdGraphPinDirection Direction) const
{
	auto Edges = GetEdgeLinkedToLayer(Layer, StartIndex, Direction);
	if (Edges.Num() == 0)
	{
		return 0.0f;
	}
	float Sum = 0.0f;
	for (auto Edge : Edges)
	{
		Sum += Edge->ToIndex;
	}
	return Sum / Edges.Num();
}

float FFormatterNode::CalcMedianValue(const TArray<FFormatterNode*>& Layer, int32 StartIndex, EEdGraphPinDirection Direction) const
{
	auto Edges = GetEdgeLinkedToLayer(Layer, StartIndex, Direction);
	float MinIndex = MAX_FLT, MaxIndex = -MAX_FLT;
	for (auto Edge : Edges)
	{
		if (Edge->FromIndex < MinIndex)
		{
			MinIndex = Edge->FromIndex;
		}
		if (Edge->FromIndex > MaxIndex)
		{
			MaxIndex = Edge->FromIndex;
		}
	}
	return (MaxIndex + MinIndex) / 2.0f;
}

int32 FFormatterNode::CalcPriority(EEdGraphPinDirection Direction) const
{
	if (OriginalNode == nullptr)
	{
		return 0;
	}
	return Direction == EGPD_Output ? OutEdges.Num() : InEdges.Num();
}

FFormatterNode::~FFormatterNode()
{
	for (auto Edge : InEdges)
	{
		delete Edge;
	}
	for (auto Edge : OutEdges)
	{
		delete Edge;
	}
	for (auto Pin : InPins)
	{
		delete Pin;
	}
	for (auto Pin : OutPins)
	{
		delete Pin;
	}
	delete SubGraph;
}

void FFormatterNode::InitPosition(FVector2D InPosition)
{
	Position = InPosition;
}

void FFormatterNode::SetPosition(FVector2D InPosition)
{
	const FVector2D Offset = InPosition - Position;
	Position = InPosition;
	if (SubGraph != nullptr)
	{
		SubGraph->OffsetBy(Offset);
	}
}

FVector2D FFormatterNode::GetPosition() const
{
	return Position;
}

void FFormatterNode::SetSubGraph(FFormatterGraph* InSubGraph)
{
	SubGraph = InSubGraph;
	auto SubGraphInPins = SubGraph->GetInputPins();
	auto SubGraphOutPins = SubGraph->GetOutputPins();
	for (auto Pin : SubGraphInPins)
	{
		auto NewPin = new FFormatterPin();
		NewPin->Guid = Pin->Guid;
		NewPin->OwningNode = this;
		NewPin->Direction = Pin->Direction;
		NewPin->NodeOffset = Pin->NodeOffset;
		NewPin->OriginalPin = Pin->OriginalPin;
		InPins.Add(NewPin);
	}
	for (auto Pin : SubGraphOutPins)
	{
		auto NewPin = new FFormatterPin();
		NewPin->Guid = Pin->Guid;
		NewPin->OwningNode = this;
		NewPin->Direction = Pin->Direction;
		NewPin->NodeOffset = Pin->NodeOffset;
		NewPin->OriginalPin = Pin->OriginalPin;
		OutPins.Add(NewPin);
	}
}

void FFormatterNode::UpdatePinsOffset()
{
	if (SubGraph != nullptr)
	{
		auto PinsOffset = SubGraph->GetPinsOffset();
		for (auto Pin : InPins)
		{
			if (PinsOffset.Contains(Pin->OriginalPin))
			{
				Pin->NodeOffset = PinsOffset[Pin->OriginalPin];
			}
		}
		for (auto Pin : OutPins)
		{
			if (PinsOffset.Contains(Pin->OriginalPin))
			{
				Pin->NodeOffset = PinsOffset[Pin->OriginalPin];
			}
		}
		InPins.Sort([](const FFormatterPin& A, const FFormatterPin& B)
		{
			return A.NodeOffset.Y < B.NodeOffset.Y;
		});
		InPins.Sort([](const FFormatterPin& A, const FFormatterPin& B)
		{
			return A.NodeOffset.Y < B.NodeOffset.Y;
		});
	}
}

TArray<FFormatterEdge> FFormatterGraph::GetEdgeForNode(FFormatterNode* Node, TSet<UEdGraphNode*> SelectedNodes)
{
	TArray<FFormatterEdge> Result;
	auto OriginalNode = Node->OriginalNode;
	if (SubGraphs.Contains(OriginalNode->NodeGuid))
	{
		const TSet<UEdGraphNode*> InnerSelectedNodes = SubGraphs[OriginalNode->NodeGuid]->GetOriginalNodes();
		for (auto SelectedNode : InnerSelectedNodes)
		{
			for (auto Pin : SelectedNode->Pins)
			{
				for (auto LinkedToPin : Pin->LinkedTo)
				{
					const auto LinkedToNode = LinkedToPin->GetOwningNodeUnchecked();
					if (InnerSelectedNodes.Contains(LinkedToNode) || !SelectedNodes.Contains(LinkedToNode))
					{
						continue;
					}
					FFormatterPin* From = OriginalPinsMap[Pin];
					FFormatterPin* To = OriginalPinsMap[LinkedToPin];
					Result.Add(FFormatterEdge{ From, 0, To, 0 });
				}
			}
		}
	}
	else
	{
		for (auto Pin : OriginalNode->Pins)
		{
			for (auto LinkedToPin : Pin->LinkedTo)
			{
				const auto LinkedToNode = LinkedToPin->GetOwningNodeUnchecked();
				if (!SelectedNodes.Contains(LinkedToNode))
				{
					continue;
				}
				FFormatterPin* From = OriginalPinsMap[Pin];
				FFormatterPin* To = OriginalPinsMap[LinkedToPin];
				Result.Add(FFormatterEdge{ From, 0, To, 0 });
			}
		}
	}
	return Result;
}

TArray<FFormatterNode*> FFormatterGraph::GetSuccessorsForNodes(TSet<FFormatterNode*> Nodes)
{
	TArray<FFormatterNode*> Result;
	for (auto Node : Nodes)
	{
		for (auto outEdge : Node->OutEdges)
		{
			if (!Nodes.Contains(outEdge->To->OwningNode))
			{
				Result.Add(outEdge->To->OwningNode);
			}
		}
	}
	return Result;
}

TArray<FFormatterNode*> FFormatterGraph::GetNodesGreaterThan(int32 i, TSet<FFormatterNode*>& Excluded)
{
	TArray<FFormatterNode*> Result;
	for (auto Node : Nodes)
	{
		if (!Excluded.Contains(Node) && Node->PathDepth >= i)
		{
			Result.Add(Node);
		}
	}
	return Result;
}

void FFormatterGraph::BuildNodes(UEdGraph* InGraph, TSet<UEdGraphNode*> SelectedNodes)
{
	TArray<UEdGraphNode_Comment*> SortedCommentNodes = GetSortedCommentNodes(InGraph, SelectedNodes);
	for (int32 i = SortedCommentNodes.Num() - 1; i != -1; --i)
	{
		UEdGraphNode_Comment* CommentNode = SortedCommentNodes[i];
		if (PickedNodes.Contains(CommentNode))
		{
			continue;
		}
		FFormatterNode* NodeData = CollapseNode(CommentNode, SelectedNodes);
		AddNode(NodeData);
		PickedNodes.Add(CommentNode);
	}
	for (auto Node : InGraph->Nodes)
	{
		if (!SelectedNodes.Contains(Node) || PickedNodes.Contains(Node))
		{
			continue;
		}
		FFormatterNode* NodeData = new FFormatterNode(Node);
		AddNode(NodeData);
		PickedNodes.Add(Node);
	}
}

void FFormatterGraph::BuildEdges(TSet<UEdGraphNode*> SelectedNodes)
{
	for (auto Node : Nodes)
	{
		auto Edges = GetEdgeForNode(Node, SelectedNodes);
		for (auto Edge : Edges)
		{
			Node->Connect(Edge.From, Edge.To);
		}
	}
}

TArray<UEdGraphNode_Comment*> FFormatterGraph::GetSortedCommentNodes(UEdGraph* InGraph, TSet<UEdGraphNode*> SelectedNodes)
{
	TArray<UEdGraphNode_Comment*> CommentNodes;
	for (auto Node : InGraph->Nodes)
	{
		if (!SelectedNodes.Contains(Node))
		{
			continue;
		}
		if (Node->IsA(UEdGraphNode_Comment::StaticClass()))
		{
			auto CommentNode = Cast<UEdGraphNode_Comment>(Node);
			CommentNodes.Add(CommentNode);
		}
	}
	CommentNodes.Sort([](const UEdGraphNode_Comment& A, const UEdGraphNode_Comment& B)
	{
		return A.CommentDepth > B.CommentDepth;
	});
	return CommentNodes;
}

TSet<UEdGraphNode*> FFormatterGraph::GetChildren(const UEdGraphNode* InNode, TSet<UEdGraphNode*> SelectedNodes) const
{
	const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(InNode);
	auto ObjectsUnderComment = CommentNode->GetNodesUnderComment();
	TSet<UEdGraphNode*> SubSelectedNodes;
	for (auto Object : ObjectsUnderComment)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (Node != nullptr && SelectedNodes.Contains(Node))
		{
			SubSelectedNodes.Add(Node);
		}
	}
	return SubSelectedNodes;
}

TSet<UEdGraphNode*> FFormatterGraph::PickChildren(const UEdGraphNode* InNode, TSet<UEdGraphNode*> SelectedNodes)
{
	const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(InNode);
	auto ObjectsUnderComment = CommentNode->GetNodesUnderComment();
	TSet<UEdGraphNode*> SubSelectedNodes;
	for (auto Object : ObjectsUnderComment)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (Node != nullptr && SelectedNodes.Contains(Node) && !PickedNodes.Contains(Node))
		{
			SubSelectedNodes.Add(Node);
			PickedNodes.Add(Node);
		}
	}
	return SubSelectedNodes;
}

TSet<UEdGraphNode*> FFormatterGraph::GetChildren(const UEdGraphNode* InNode) const
{
	const UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(InNode);
	auto ObjectsUnderComment = CommentNode->GetNodesUnderComment();
	TSet<UEdGraphNode*> SubSelectedNodes;
	for (auto Object : ObjectsUnderComment)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(Object);
		if (Node != nullptr)
		{
			SubSelectedNodes.Add(Node);
		}
	}
	return SubSelectedNodes;
}

FFormatterGraph* FFormatterGraph::BuildSubGraph(const UEdGraphNode* InNode, TSet<UEdGraphNode*> SelectedNodes)
{
	TSet<UEdGraphNode*> SubSelectedNodes = PickChildren(InNode, SelectedNodes);
	if (SubSelectedNodes.Num() > 0)
	{
		FFormatterGraph* SubGraph = new FFormatterGraph(UEGraph, SubSelectedNodes, Delegates);
		return SubGraph;
	}
	return nullptr;
}

FFormatterNode* FFormatterGraph::CollapseNode(UEdGraphNode* InNode, TSet<UEdGraphNode*> SelectedNodes)
{
	FFormatterNode* Node = new FFormatterNode(InNode);
	FFormatterGraph* SubGraph = BuildSubGraph(InNode, SelectedNodes);
	if (SubGraph != nullptr)
	{
		Node->SetSubGraph(SubGraph);
	}
	return Node;
}

void FFormatterGraph::AddNode(FFormatterNode* InNode)
{
	Nodes.Add(InNode);
	NodesMap.Add(InNode->Guid, InNode);
	if (InNode->SubGraph != nullptr)
	{
		SubGraphs.Add(InNode->Guid, InNode->SubGraph);
	}
	for (auto Pin : InNode->InPins)
	{
		if (Pin->OriginalPin != nullptr)
		{
			OriginalPinsMap.Add(Pin->OriginalPin, Pin);
		}
		PinsMap.Add(Pin->Guid, Pin);
	}
	for (auto Pin : InNode->OutPins)
	{
		if (Pin->OriginalPin != nullptr)
		{
			OriginalPinsMap.Add(Pin->OriginalPin, Pin);
		}
		PinsMap.Add(Pin->Guid, Pin);
	}
}

void FFormatterGraph::RemoveNode(FFormatterNode* NodeToRemove)
{
	TArray<FFormatterEdge*> Edges = NodeToRemove->InEdges;
	for (auto Edge : Edges)
	{
		Edge->To->OwningNode->Disconnect(Edge->To, Edge->From);
	}
	Edges = NodeToRemove->OutEdges;
	for (auto Edge : Edges)
	{
		Edge->To->OwningNode->Disconnect(Edge->To, Edge->From);
	}
	Nodes.Remove(NodeToRemove);
	NodesMap.Remove(NodeToRemove->Guid);
	SubGraphs.Remove(NodeToRemove->Guid);
	for (auto Pin : NodeToRemove->InPins)
	{
		OriginalPinsMap.Remove(Pin->OriginalPin);
		PinsMap.Remove(Pin->Guid);
	}
	for (auto Pin : NodeToRemove->OutPins)
	{
		OriginalPinsMap.Remove(Pin->OriginalPin);
		PinsMap.Remove(Pin->Guid);
	}
	delete NodeToRemove;
}

void FFormatterGraph::RemoveCycle()
{
	auto ClonedGraph = new FFormatterGraph(*this);
	while (auto SourceNode = ClonedGraph->FindSourceNode())
	{
		ClonedGraph->RemoveNode(SourceNode);
	}
	while (auto SinkNode = ClonedGraph->FindSinkNode())
	{
		ClonedGraph->RemoveNode(SinkNode);
	}
	while (auto MedianNode = ClonedGraph->FindMedianNode())
	{
		for (auto Edge : MedianNode->InEdges)
		{
			FFormatterPin* From = PinsMap[Edge->From->Guid];
			FFormatterPin* To = PinsMap[Edge->To->Guid];
			NodesMap[MedianNode->Guid]->Disconnect(From, To);
			To->OwningNode->Disconnect(To, From);
		}
		ClonedGraph->RemoveNode(MedianNode);
	}
	delete ClonedGraph;
}

FFormatterNode* FFormatterGraph::FindSourceNode() const
{
	for (auto Node : Nodes)
	{
		if (Node->IsSource())
		{
			return Node;
		}
	}
	return nullptr;
}

FFormatterNode* FFormatterGraph::FindSinkNode() const
{
	for (auto Node : Nodes)
	{
		if (Node->IsSink())
		{
			return Node;
		}
	}
	return nullptr;
}

FFormatterNode* FFormatterGraph::FindMedianNode() const
{
	FFormatterNode* Result = nullptr;
	int32 MaxDegreeDiff = 0;
	for (auto Node : Nodes)
	{
		const int32 DegreeDiff = Node->OutEdges.Num() - Node->InEdges.Num();
		if (DegreeDiff >= MaxDegreeDiff)
		{
			MaxDegreeDiff = DegreeDiff;
			Result = Node;
		}
	}
	return Result;
}

void FFormatterGraph::BuildNodesAndEdges(UEdGraph* InGraph, TSet<UEdGraphNode*> SelectedNodes)
{
	BuildNodes(InGraph, SelectedNodes);
	BuildEdges(SelectedNodes);
	Nodes.Sort([](const FFormatterNode& A, const FFormatterNode& B)
	{
		return A.GetPosition().Y < B.GetPosition().Y;
	});
}

FFormatterGraph::FFormatterGraph(UEdGraph* InGraph, const TSet<UEdGraphNode*>& SelectedNodes, FFormatterDelegates InDelegates, bool IsSingleMode)
{
	UEGraph = InGraph;
	Delegates = InDelegates;
	if (IsSingleMode)
	{
		BuildNodesAndEdges(InGraph, SelectedNodes);
	}
	else
	{
		auto FoundIsolatedGraphs = FindIsolated(InGraph, SelectedNodes);
		if (FoundIsolatedGraphs.Num() > 1)
		{
			for (const auto& IsolatedNodes : FoundIsolatedGraphs)
			{
				auto NewGraph = new FFormatterGraph(InGraph, IsolatedNodes, InDelegates);
				IsolatedGraphs.Add(NewGraph);
			}
		}
		else if (FoundIsolatedGraphs.Num() == 1)
		{
			BuildNodesAndEdges(InGraph, FoundIsolatedGraphs[0]);
		}
	}
}

FFormatterGraph::FFormatterGraph(const FFormatterGraph& Other)
{
	UEGraph = Other.UEGraph;
	Delegates = Other.Delegates;
	for (auto Node : Other.Nodes)
	{
		FFormatterNode* Cloned = new FFormatterNode(*Node);
		AddNode(Cloned);
	}
	for (auto Node : Other.Nodes)
	{
		for (auto Edge : Node->InEdges)
		{
			FFormatterPin* From = PinsMap[Edge->From->Guid];
			FFormatterPin* To = PinsMap[Edge->To->Guid];
			NodesMap[Node->Guid]->Connect(From, To);
		}
		for (auto Edge : Node->OutEdges)
		{
			FFormatterPin* From = PinsMap[Edge->From->Guid];
			FFormatterPin* To = PinsMap[Edge->To->Guid];
			NodesMap[Node->Guid]->Connect(From, To);
		}
	}
	for (auto Isolated : Other.IsolatedGraphs)
	{
		IsolatedGraphs.Add(new FFormatterGraph(*Isolated));
	}
}

FFormatterGraph::~FFormatterGraph()
{
	for (auto Node : Nodes)
	{
		delete Node;
	}
	for (auto Graph : IsolatedGraphs)
	{
		delete Graph;
	}
}

TArray<TSet<UEdGraphNode*>> FFormatterGraph::FindIsolated(UEdGraph* InGraph, const TSet<UEdGraphNode*>& SelectedNodes)
{
	TArray<TSet<UEdGraphNode*>> Result;
	TSet<FFormatterNode*> CheckedNodes;
	TArray<FFormatterNode*> Stack;
	auto TempGraph = FFormatterGraph(InGraph, SelectedNodes, FFormatterDelegates(), true);
	for (auto Node : TempGraph.Nodes)
	{
		if (!CheckedNodes.Contains(Node))
		{
			CheckedNodes.Add(Node);
			Stack.Push(Node);
		}
		TSet<UEdGraphNode*> IsolatedNodes;
		while (Stack.Num() != 0)
		{
			FFormatterNode* Top = Stack.Pop();
			IsolatedNodes.Add(Top->OriginalNode);
			if (Top->SubGraph != nullptr)
			{
				IsolatedNodes.Append(Top->SubGraph->GetOriginalNodes());
			}
			TArray<FFormatterNode*> ConnectedNodes = Top->GetSuccessors();
			TArray<FFormatterNode*> Predecessors = Top->GetPredecessors();
			ConnectedNodes.Append(Predecessors);
			for (auto ConnectedNode : ConnectedNodes)
			{
				if (!CheckedNodes.Contains(ConnectedNode))
				{
					Stack.Push(ConnectedNode);
					CheckedNodes.Add(ConnectedNode);
				}
			}
		}
		if (IsolatedNodes.Num() != 0)
		{
			Result.Add(IsolatedNodes);
		}
	}
	return Result;
}

int32 FFormatterGraph::CalculateLongestPath() const
{
	int32 LongestPath = 1;
	while (true)
	{
		auto Leaves = GetLeavesWidthPathDepthEqu0();
		if (Leaves.Num() == 0)
		{
			break;
		}
		for (auto leaf : Leaves)
		{
			leaf->PathDepth = LongestPath;
		}
		LongestPath++;
	}
	LongestPath--;
	return LongestPath;
}

void FFormatterGraph::CalculatePinsIndex() const
{
	for (int i = 0; i < LayeredList.Num(); i++)
	{
		auto& Layer = LayeredList[i];
		for (int j = 0; j < Layer.Num(); j++)
		{

		}
	}
}

TArray<FFormatterNode*> FFormatterGraph::GetLeavesWidthPathDepthEqu0() const
{
	TArray<FFormatterNode*> Result;
	for (auto Node : Nodes)
	{
		if (Node->PathDepth != 0 || Node->AnySuccessorPathDepthEqu0())
		{
			continue;
		}
		Result.Add(Node);
	}
	return Result;
}

void FFormatterGraph::DoLayering()
{
	LayeredList.Empty();
	TSet<FFormatterNode*> Set;
	for (int32 i = CalculateLongestPath(); i != 0; i--)
	{
		TSet<FFormatterNode*> Layer;
		auto Successors = GetSuccessorsForNodes(Set);
		auto NodesToProcess = GetNodesGreaterThan(i, Set);
		NodesToProcess.Append(Successors);
		for (auto Node : NodesToProcess)
		{
			auto Predecessors = Node->GetPredecessors();
			bool bPredecessorsFinished = true;
			for (auto Predecessor : Predecessors)
			{
				if (!Set.Contains(Predecessor))
				{
					bPredecessorsFinished = false;
					break;
				}
			}
			if (bPredecessorsFinished)
			{
				Layer.Add(Node);
			}
		}
		Set.Append(Layer);
		LayeredList.Add(Layer.Array());
	}
}

void FFormatterGraph::AddDummyNodes()
{
	for (int i = 0; i < LayeredList.Num() - 1; i++)
	{
		auto& Layer = LayeredList[i];
		auto& NextLayer = LayeredList[i + 1];
		for (auto Node : Layer)
		{
			TArray<FFormatterEdge*> LongEdges;
			for (auto Edge : Node->OutEdges)
			{
				if (!NextLayer.Contains(Edge->To->OwningNode))
				{
					LongEdges.Add(Edge);
				}
			}
			for (auto Edge : LongEdges)
			{
				Node->Disconnect(Edge->From, Edge->To);
				auto dummyNode = new FFormatterNode();
				AddNode(dummyNode);
				Node->Connect(Edge->From, dummyNode->InPins[0]);
				dummyNode->Connect(dummyNode->InPins[0], Edge->From);
				dummyNode->Connect(dummyNode->OutPins[0], Edge->To);
				Edge->To->OwningNode->Connect(Edge->To, dummyNode->OutPins[0]);
				NextLayer.Add(dummyNode);
			}
		}
	}
}

void FFormatterGraph::SortInLayer(TArray<TArray<FFormatterNode*>>& Order, EEdGraphPinDirection Direction)
{
	if (Order.Num() < 2)
	{
		return;
	}
	const int Start = Direction == EGPD_Output ? Order.Num() - 2 : 1;
	const int End = Direction == EGPD_Output ? -1 : Order.Num();
	const int Step = Direction == EGPD_Output ? -1 : 1;
	for (int i = Start; i != End; i += Step)
	{
		auto& FixedLayer = Order[i - Step];
		auto& FreeLayer = Order[i];
		int32 StartIndex = 0;
		for (FFormatterNode* Node : FreeLayer)
		{
			Node->OrderValue = Node->CalcBarycenter(FixedLayer, StartIndex, Direction);
			StartIndex += Direction == EGPD_Output ? Node->GetOutputPinCount() : Node->GetInputPinCount();
		}
		FreeLayer.Sort([](const FFormatterNode& A, const FFormatterNode& B)-> bool
		{
			return A.OrderValue < B.OrderValue;
		});
	}
}

static TArray<FFormatterEdge*> GetEdgeBetweenTwoLayer(const TArray<FFormatterNode*>& Layer1, const TArray<FFormatterNode*>& Layer2, EEdGraphPinDirection Direction)
{
	int32 Index = 0;
	TArray<FFormatterEdge*> Result;
	for (auto NodeInLayer1 : Layer1)
	{
		Result += NodeInLayer1->GetEdgeLinkedToLayer(Layer2, Index, Direction);
		Index += Direction == EGPD_Output ? NodeInLayer1->GetOutputPinCount() : NodeInLayer1->GetInputPinCount();
	}
	return Result;
}

static int32 CalculateCrossing(const TArray<TArray<FFormatterNode*>>& Order)
{
	int32 CrossingValue = 0;
	for (int i = 1; i < Order.Num(); i++)
	{
		const auto& Layer = Order[i - 1];
		const auto& NextLayer = Order[i];
		TArray<FFormatterEdge*> NodeEdges = GetEdgeBetweenTwoLayer(Layer, NextLayer, EGPD_Output);
		while (NodeEdges.Num() != 0)
		{
			const auto Edge1 = NodeEdges.Pop();
			for (const auto Edge2 : NodeEdges)
			{
				if (Edge1->IsCrossing(Edge2))
				{
					CrossingValue++;
				}
			}
		}
	}
	return CrossingValue;
}

void FFormatterGraph::DoOrderingSweep()
{
	const UFormatterSettings* Settings = GetDefault<UFormatterSettings>();
	auto Best = LayeredList;
	auto Order = LayeredList;
	for (int i = 0; i < Settings->MaxOrderingIterations; i++)
	{
		SortInLayer(Order, i % 2 == 0 ? EGPD_Input : EGPD_Output);
		if (CalculateCrossing(Order) < CalculateCrossing(Best))
		{
			Best = Order;
		}
	}
	LayeredList = Best;
}

void FFormatterGraph::DoPositioning()
{
	const UFormatterSettings& Settings = *GetDefault<UFormatterSettings>();
	if (Settings.PositioningAlgorithm == EGraphFormatterPositioningAlgorithm::EEvenlyInLayer)
	{
		FEvenlyPlaceStrategy LeftToRightPositioningStrategy(LayeredList);
		TotalBound = LeftToRightPositioningStrategy.GetTotalBound();
	}
	if (Settings.PositioningAlgorithm == EGraphFormatterPositioningAlgorithm::EPriorityMethod)
	{
		FPriorityPositioningStrategy PriorityPositioningStrategy(LayeredList);
		TotalBound = PriorityPositioningStrategy.GetTotalBound();
	}
}

TMap<UEdGraphPin*, FVector2D> FFormatterGraph::GetPinsOffset()
{
	TMap<UEdGraphPin*, FVector2D> Result;
	const UFormatterSettings& Settings = *GetDefault<UFormatterSettings>();
	const auto Border = FVector2D(Settings.CommentBorder, Settings.CommentBorder);
	if (IsolatedGraphs.Num() > 0)
	{
		for (auto IsolatedGraph : IsolatedGraphs)
		{
			auto SubBound = IsolatedGraph->GetTotalBound();
			auto Offset = SubBound.GetTopLeft() - TotalBound.GetTopLeft();
			auto SubOffsets = IsolatedGraph->GetPinsOffset();
			for (auto SubOffsetPair : SubOffsets)
			{
				SubOffsetPair.Value = SubOffsetPair.Value + Offset;
			}
			Result.Append(SubOffsets);
		}
		return Result;
	}
	for (auto Node : Nodes)
	{
		for (auto OutPin : Node->OutPins)
		{
			FVector2D PinOffset = Node->Position + OutPin->NodeOffset - TotalBound.GetTopLeft() + Border;
			Result.Add(OutPin->OriginalPin, PinOffset);
		}
		for (auto InPin : Node->InPins)
		{
			FVector2D PinOffset = Node->Position + InPin->NodeOffset - TotalBound.GetTopLeft() + Border;
			Result.Add(InPin->OriginalPin, PinOffset);
		}
	}
	return Result;
}

TArray<FFormatterPin*> FFormatterGraph::GetInputPins() const
{
	TSet<FFormatterPin*> Result;
	if (IsolatedGraphs.Num() > 0)
	{
		for (auto IsolatedGraph : IsolatedGraphs)
		{
			Result.Append(IsolatedGraph->GetInputPins());
		}
		return Result.Array();
	}
	for (auto Node : Nodes)
	{
		for (auto Pin : Node->InPins)
		{
			Result.Add(Pin);
		}
	}
	return Result.Array();
}

TArray<FFormatterPin*> FFormatterGraph::GetOutputPins() const
{
	TSet<FFormatterPin*> Result;
	if (IsolatedGraphs.Num() > 0)
	{
		for (auto IsolatedGraph : IsolatedGraphs)
		{
			Result.Append(IsolatedGraph->GetOutputPins());
		}
		return Result.Array();
	}
	for (auto Node : Nodes)
	{
		for (auto Pin : Node->OutPins)
		{
			Result.Add(Pin);
		}
	}
	return Result.Array();
}

TSet<UEdGraphNode*> FFormatterGraph::GetOriginalNodes() const
{
	TSet<UEdGraphNode*> Result;
	if (IsolatedGraphs.Num() > 0)
	{
		for (auto IsolatedGraph : IsolatedGraphs)
		{
			Result.Append(IsolatedGraph->GetOriginalNodes());
		}
		return Result;
	}
	for (auto Node : Nodes)
	{
		if (SubGraphs.Contains(Node->Guid))
		{
			Result.Append(SubGraphs[Node->Guid]->GetOriginalNodes());
		}
		if (Node->OriginalNode != nullptr)
		{
			Result.Add(Node->OriginalNode);
		}
	}
	return Result;
}

void FFormatterGraph::CalculateNodesSize(FCalculateNodeBoundDelegate SizeCalculator)
{
	if (IsolatedGraphs.Num() > 1)
	{
		for (auto IsolatedGraph : IsolatedGraphs)
		{
			IsolatedGraph->CalculateNodesSize(SizeCalculator);
		}
	}
	else
	{
		for (auto Node : Nodes)
		{
			if (Node->OriginalNode != nullptr)
			{
				if (SubGraphs.Contains(Node->Guid))
				{
					SubGraphs[Node->Guid]->CalculateNodesSize(SizeCalculator);
				}
				Node->Size = SizeCalculator.Execute(Node->OriginalNode);
			}
		}
	}
}

void FFormatterGraph::CalculatePinsOffset(FOffsetCalculatorDelegate OffsetCalculator)
{
	if (IsolatedGraphs.Num() > 1)
	{
		for (auto IsolatedGraph : IsolatedGraphs)
		{
			IsolatedGraph->CalculatePinsOffset(OffsetCalculator);
		}
	}
	else
	{
		for (auto Node : Nodes)
		{
			if (Node->OriginalNode != nullptr)
			{
				if (SubGraphs.Contains(Node->Guid))
				{
					SubGraphs[Node->Guid]->CalculatePinsOffset(OffsetCalculator);
				}
				for (auto Pin : Node->InPins)
				{
					Pin->NodeOffset = OffsetCalculator.Execute(Pin->OriginalPin);
				}
				for (auto Pin : Node->OutPins)
				{
					Pin->NodeOffset = OffsetCalculator.Execute(Pin->OriginalPin);
				}
			}
		}
	}
}

void FFormatterGraph::Format()
{
	const UFormatterSettings& Settings = *GetDefault<UFormatterSettings>();
	if (IsolatedGraphs.Num() > 1)
	{
		FSlateRect PreBound;
		for (auto isolatedGraph : IsolatedGraphs)
		{
			isolatedGraph->Format();
			if (PreBound.IsValid())
			{
				isolatedGraph->SetPosition(PreBound.GetBottomLeft());
			}
			auto Bound = isolatedGraph->GetTotalBound();
			if (TotalBound.IsValid())
			{
				TotalBound = TotalBound.Expand(Bound);
			}
			else
			{
				TotalBound = Bound;
			}
			PreBound = TotalBound.OffsetBy(FVector2D(0, Settings.VerticalSpacing));
		}
	}
	else
	{
		CalculateNodesSize(Delegates.BoundCalculator);
		CalculatePinsOffset(Delegates.OffsetCalculator);
		for (auto SubGraphPair : SubGraphs)
		{
			auto SubGraph = SubGraphPair.Value;
			auto Node = NodesMap[SubGraphPair.Key];
			SubGraph->Format();
			Node->UpdatePinsOffset();
			auto Bound = SubGraph->GetTotalBound();
			Node->InitPosition(Bound.GetTopLeft() - FVector2D(Settings.CommentBorder, Settings.CommentBorder));
			Node->Size = SubGraph->GetTotalBound().GetSize() + FVector2D(Settings.CommentBorder * 2, Settings.CommentBorder * 2);
		}
		if (Nodes.Num() > 0)
		{
			RemoveCycle();
			DoLayering();
			AddDummyNodes();
			DoOrderingSweep();
			DoPositioning();
		}
	}
}

FSlateRect FFormatterGraph::GetTotalBound() const
{
	return TotalBound;
}

void FFormatterGraph::OffsetBy(const FVector2D& InOffset)
{
	if (IsolatedGraphs.Num() > 0)
	{
		for (auto isolatedGraph : IsolatedGraphs)
		{
			isolatedGraph->OffsetBy(InOffset);
		}
	}
	else
	{
		for (auto Node : Nodes)
		{
			Node->SetPosition(Node->GetPosition() + InOffset);
		}
	}
	TotalBound = TotalBound.OffsetBy(InOffset);
}

void FFormatterGraph::SetPosition(const FVector2D& Position)
{
	const FVector2D Offset = Position - TotalBound.GetTopLeft();
	OffsetBy(Offset);
}

TMap<UEdGraphNode*, FSlateRect> FFormatterGraph::GetBoundMap()
{
	TMap<UEdGraphNode*, FSlateRect> Result;
	if (IsolatedGraphs.Num() > 0)
	{
		for (auto Graph : IsolatedGraphs)
		{
			Result.Append(Graph->GetBoundMap());
		}
		return Result;
	}
	for (auto Node : Nodes)
	{
		if (Node->OriginalNode == nullptr)
		{
			continue;
		}
		Result.Add(Node->OriginalNode, FSlateRect::FromPointAndExtent(Node->GetPosition(), Node->Size));
		if (SubGraphs.Contains(Node->Guid))
		{
			Result.Append(SubGraphs[Node->Guid]->GetBoundMap());
		}
	}
	return Result;
}
