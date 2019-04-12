﻿#include "FastAndSimplePositioningStrategy.h"
#include "FormatterGraph.h"
#include "FormatterSettings.h"

void FFastAndSimplePositioningStrategy::Initialize()
{
	for (auto& Layer : LayeredNodes)
	{
		for (int32 i = 0; i < Layer.Num(); i++)
		{
			PosMap.Add(Layer[i], i);
			if (i != 0)
			{
				PredecessorMap.Add(Layer[i], Layer[i - 1]);
			}
			else
			{
				PredecessorMap.Add(Layer[i], nullptr);
			}
			if (i != Layer.Num() - 1)
			{
				SuccessorMap.Add(Layer[i], Layer[i + 1]);
			}
			else
			{
				SuccessorMap.Add(Layer[i], nullptr);
			}
		}
	}
	MarkConflicts();
}

void FFastAndSimplePositioningStrategy::MarkConflicts()
{
	for (int32 i = 1; i < LayeredNodes.Num() - 1; i++)
	{
		int32 k0 = 0;
		int32 l = 1;
		for (int32 l1 = 0; l1 < LayeredNodes[i + 1].Num(); l1++)
		{
			auto Node = LayeredNodes[i + 1][l1];
			bool IsCrossingInnerSegment = Node->IsCrossingInnerSegment(LayeredNodes[i + 1], LayeredNodes[i]);
			if (l1 == LayeredNodes[i + 1].Num() - 1 || IsCrossingInnerSegment)
			{
				int32 k1 = LayeredNodes[i].Num();
				if (IsCrossingInnerSegment)
				{
					const auto MedianUpper = Node->GetMedianUpper();
					k1 = PosMap[MedianUpper];
				}
				while (l < l1)
				{
					auto UpperNodes = Node->GetUppers();
					for (auto UpperNode : UpperNodes)
					{
						auto k = PosMap[UpperNode];
						if (k < k0 || k > k1)
						{
							ConflictMarks.Add(UpperNode, Node);
						}
					}
					++l;
				}
				k0 = k1;
			}
		}
	}
}

void FFastAndSimplePositioningStrategy::DoVerticalAlignment()
{
	RootMap.Empty();
	AlignMap.Empty();
	for (auto Layer : LayeredNodes)
	{
		for (auto Node : Layer)
		{
			RootMap.Add(Node, Node);
			AlignMap.Add(Node, Node);
		}
	}
	int32 LayerStep = IsUpperDirection ? 1 : -1;
	int32 LayerStart = IsUpperDirection ? 0 : LayeredNodes.Num() - 1;
	int32 LayerEnd = IsUpperDirection ? LayeredNodes.Num() : -1;
	for (int32 i = LayerStart; i != LayerEnd; i += LayerStep)
	{
		int32 Guide = IsLeftDirection ? -1 : INT_MAX;
		int32 Step = IsLeftDirection ? 1 : -1;
		int32 Start = IsLeftDirection ? 0 : LayeredNodes[i].Num() - 1;
		int32 End = IsLeftDirection ? LayeredNodes[i].Num() : -1;
		for (int32 k = Start; k != End; k += Step)
		{
			auto Node = LayeredNodes[i][k];
			auto Adjacencies = IsUpperDirection ? Node->GetUppers() : Node->GetLowers();
			if (Adjacencies.Num() > 0)
			{
				int32 ma = FMath::TruncToInt((Adjacencies.Num() + 1) / 2.0f - 1);
				int32 mb = FMath::CeilToInt((Adjacencies.Num() + 1) / 2.0f - 1);
				for (int32 m = ma; m <= mb; m++)
				{
					auto& MedianNode = Adjacencies[m];
					if (AlignMap[Node] == Node)
					{
						bool IsMarked = ConflictMarks.Contains(MedianNode) && ConflictMarks[MedianNode] == Node;
						const auto MedianNodePos = PosMap[MedianNode];
						bool IsGuideAccepted = IsLeftDirection ? MedianNodePos > Guide : MedianNodePos < Guide;
						if (!IsMarked && IsGuideAccepted)
						{
							AlignMap[MedianNode] = Node;
							RootMap[Node] = RootMap[MedianNode];
							AlignMap[Node] = RootMap[Node];
							Guide = MedianNodePos;
						}
					}
				}
			}
		}
	}
}

void FFastAndSimplePositioningStrategy::DoHorizontalCompaction()
{
	SinkMap.Empty();
	ShiftMap.Empty();
	XMap->Empty();
	for (auto& Layer : LayeredNodes)
	{
		for (auto Node : Layer)
		{
			SinkMap.Add(Node, Node);
			ShiftMap.Add(Node, FLT_MAX);
			XMap->Add(Node, NAN);
		}
	}
	for (auto& Layer : LayeredNodes)
	{
		for (auto Node : Layer)
		{
			if (RootMap[Node] == Node)
			{
				PlaceBlock(Node);
			}
		}
	}
	for (auto& Layer : LayeredNodes)
	{
		for (auto Node : Layer)
		{
			auto& RootNode = RootMap[Node];
			(*XMap)[Node] = (*XMap)[RootNode];
		}
	}
	for(auto& Layer : LayeredNodes)
	{
		for (auto Node : Layer)
		{
			auto& RootNode = RootMap[Node];
			const float Shift = ShiftMap[SinkMap[RootNode]];
			if (Shift < FLT_MAX)
			{
				(*XMap)[Node] = (*XMap)[Node] + Shift;
			}
		}
	}
	for (auto& Layer : LayeredNodes)
	{
		for (auto Node : Layer)
		{
			(*XMap)[Node] += InnerShiftMap[Node];
		}
	}
}

void FFastAndSimplePositioningStrategy::PlaceBlock(FFormatterNode* BlockRoot)
{
	const UFormatterSettings& Settings = *GetDefault<UFormatterSettings>();
	if (FMath::IsNaN((*XMap)[BlockRoot]))
	{
		bool Initial = true;
		(*XMap)[BlockRoot] = 0;
		auto Node = BlockRoot;
		do
		{
			const auto Adjacency = IsLeftDirection ? PredecessorMap[Node] : SuccessorMap[Node];
			if (Adjacency != nullptr)
			{
				const auto PrevBlockRoot = RootMap[Adjacency];
				PlaceBlock(PrevBlockRoot);
				if (SinkMap[BlockRoot] == BlockRoot)
				{
					SinkMap[BlockRoot] = SinkMap[PrevBlockRoot];
				}
				if (SinkMap[BlockRoot] != SinkMap[PrevBlockRoot])
				{
					float LeftShift = (*XMap)[BlockRoot] - (*XMap)[PrevBlockRoot] + InnerShiftMap[Node] - InnerShiftMap[Adjacency] - Adjacency->Size.Y - Settings.VerticalSpacing;
					float RightShift = (*XMap)[PrevBlockRoot] - (*XMap)[BlockRoot] - InnerShiftMap[Node] + InnerShiftMap[Adjacency] - Node->Size.Y + Settings.VerticalSpacing;
					float Shift = IsLeftDirection ? FMath::Min(ShiftMap[SinkMap[PrevBlockRoot]], LeftShift) : FMath::Max(ShiftMap[SinkMap[PrevBlockRoot]], RightShift);
					ShiftMap[SinkMap[PrevBlockRoot]] = Shift;
				}
				else
				{
					float LeftShift = InnerShiftMap[Adjacency] + Adjacency->Size.Y - InnerShiftMap[Node] + Settings.VerticalSpacing;
					float RightShift = -Node->Size.Y - Settings.VerticalSpacing + InnerShiftMap[Adjacency] - InnerShiftMap[Node];
					float Shift = IsLeftDirection ? LeftShift : RightShift;
					float Position = (*XMap)[PrevBlockRoot] + Shift;
					if (Initial)
					{
						(*XMap)[BlockRoot] = Position;
						Initial = false;
					}
					else
					{
						Position = IsLeftDirection ? FMath::Max((*XMap)[BlockRoot], Position) : FMath::Min((*XMap)[BlockRoot], Position);
						(*XMap)[BlockRoot] = Position;
					}
				}
			}
			Node = AlignMap[Node];
		}
		while (Node != BlockRoot);
	}
}

void FFastAndSimplePositioningStrategy::CalculateInnerShift()
{
	InnerShiftMap.Empty();
	BlockWidthMap.Empty();
	for (auto& Layer : LayeredNodes)
	{
		for (auto Node : Layer)
		{
			if (RootMap[Node] == Node)
			{
				InnerShiftMap.Add(Node, 0);
				float Left = 0, Right = Node->Size.Y;
				auto RootNode = Node;
				auto UpperNode = Node;
				auto LowerNode = AlignMap[RootNode];
				while (true)
				{
					const float UpperPosition = UpperNode->GetLinkedPositionToNode(LowerNode, IsUpperDirection ? EGPD_Output : EGPD_Input);
					const float LowerPosition = LowerNode->GetLinkedPositionToNode(UpperNode, IsUpperDirection ? EGPD_Input : EGPD_Output);
					const float Shift = InnerShiftMap[UpperNode] + UpperPosition - LowerPosition;
					InnerShiftMap.FindOrAdd(LowerNode) = Shift;
					Left = FMath::Min(Left, Shift);
					Right = FMath::Max(Right, Shift + LowerNode->Size.Y);
					UpperNode = LowerNode;
					LowerNode = AlignMap[UpperNode];
					if(LowerNode == RootNode)
					{
						break;
					}
				}
				auto CheckNode = Node;
				do
				{
					InnerShiftMap[CheckNode] -= Left;
					CheckNode = AlignMap[CheckNode];
				}
				while (CheckNode != Node);
				BlockWidthMap.FindOrAdd(Node) = Right - Left;
			}
		}
	}
}

void FFastAndSimplePositioningStrategy::Sweep()
{
	IsUpperDirection = true;
	IsLeftDirection = true;
	XMap = &UpperLeftPositionMap;
	DoOnePass();

	IsUpperDirection = true;
	IsLeftDirection = false;
	XMap = &UpperRightPositionMap;
	DoOnePass();

	IsUpperDirection = false;
	IsLeftDirection = true;
	XMap = &LowerLeftPositionMap;
	DoOnePass();

	IsUpperDirection = false;
	IsLeftDirection = false;
	XMap = &LowerRightPositionMap;
	DoOnePass();

	Combine();
}

void FFastAndSimplePositioningStrategy::Combine()
{
	TArray<TMap<FFormatterNode*, float>> Layouts = {UpperLeftPositionMap, UpperRightPositionMap, LowerLeftPositionMap, LowerRightPositionMap};
	TArray<TTuple<float, float>> Bounds;
	Bounds.SetNumUninitialized(Layouts.Num());
	int32 MinWidthIndex = -1;
	float MinWidth = FLT_MAX;
	for (int32 i = 0; i < Layouts.Num(); i++)
	{
		auto& Layout = Layouts[i];
		float LeftMost = FLT_MAX, RightMost = -FLT_MAX;
		for (auto& Pair : Layout)
		{
			if (Pair.Value < LeftMost)
			{
				LeftMost = Pair.Value;
			}
			if (Pair.Value > RightMost)
			{
				RightMost = Pair.Value;
			}
		}
		if(RightMost - LeftMost < MinWidth)
		{
			MinWidth = RightMost - LeftMost;
			MinWidthIndex = i;
		}
		Bounds[i] = TTuple<float, float>(LeftMost, RightMost);
	}
	for (int32 i = 0; i < Layouts.Num(); i++)
	{
		if (i != MinWidthIndex)
		{
			float Offset = Bounds[MinWidthIndex].Get<0>() - Bounds[i].Get<0>();
			for (auto& Pair : Layouts[i])
			{
				Pair.Value += Offset;
			}
		}
	}
	const UFormatterSettings& Settings = *GetDefault<UFormatterSettings>();
	for (auto& Layer : LayeredNodes)
	{
		for (auto Node : Layer)
		{
			TArray<float> Values = {Layouts[0][Node], Layouts[1][Node], Layouts[2][Node], Layouts[3][Node]};
			Values.Sort();
			if (Settings.PositioningAlgorithm == EGraphFormatterPositioningAlgorithm::EFastAndSimpleMethodTop)
			{
				CombinedPositionMap.Add(Node, (Values[1] + Values[2]) / 2.0f);
			}
			else if (Settings.PositioningAlgorithm == EGraphFormatterPositioningAlgorithm::EFastAndSimpleMethodMedian)
			{
				CombinedPositionMap.Add(Node, (Values[0] + Values[3]) / 2.0f);
			}
		}
	}
	XMap = &CombinedPositionMap;
}

void FFastAndSimplePositioningStrategy::DoOnePass()
{
	DoVerticalAlignment();
	CalculateInnerShift();
	DoHorizontalCompaction();
}

FFastAndSimplePositioningStrategy::FFastAndSimplePositioningStrategy(TArray<TArray<FFormatterNode*>>& InLayeredNodes)
	: IPositioningStrategy(InLayeredNodes)
{
	const auto LayersBound = FFormatterGraph::CalculateLayersBound(InLayeredNodes);
	FFormatterNode* FirstNode = InLayeredNodes[0][0];
	const FVector2D OldPosition = FirstNode->GetPosition();
	Initialize();
	Sweep();
	for (int i = 0; i < LayeredNodes.Num(); i++)
	{
		auto& Layer = LayeredNodes[i];
		for (auto Node : Layer)
		{
			float X;
			if (Node->InEdges.Num() == 0)
			{
				X = LayersBound[i].GetTopRight().X - Node->Size.X;
			}
			else
			{
				X = LayersBound[i].GetTopLeft().X;
			}
			float Y = (*XMap)[Node];
			Node->SetPosition(FVector2D(X, Y));
		}
	}
	const FVector2D NewPosition = FirstNode->GetPosition();
	const FVector2D Offset = OldPosition - NewPosition;
	FSlateRect Bound;
	for (int32 i = 0; i < InLayeredNodes.Num(); i++)
	{
		for (auto Node : InLayeredNodes[i])
		{
			Node->SetPosition(Node->GetPosition() + Offset);
			if (Bound.IsValid())
			{
				Bound = Bound.Expand(FSlateRect::FromPointAndExtent(Node->GetPosition(), Node->Size));
			}
			else
			{
				Bound = FSlateRect::FromPointAndExtent(Node->GetPosition(), Node->Size);
			}
		}
	}
	TotalBound = Bound;
}