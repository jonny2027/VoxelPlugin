// Copyright 2020 Phyronnaz

#include "VoxelRender/Meshers/VoxelGreedyCubicMesher.h"
#include "VoxelRender/Meshers/VoxelMesherUtilities.h"
#include "VoxelRender/IVoxelRenderer.h"
#include "VoxelRender/VoxelChunkMesh.h"
#include "VoxelData/VoxelDataIncludes.h"
#include "VoxelIntBox.inl"

FVoxelIntBox FVoxelGreedyCubicMesher::GetBoundsToCheckIsEmptyOn() const
{
	return FVoxelIntBox(ChunkPosition - FIntVector(Step), ChunkPosition - FIntVector(Step) + CUBIC_CHUNK_SIZE_WITH_NEIGHBORS * Step);
}

FVoxelIntBox FVoxelGreedyCubicMesher::GetBoundsToLock() const
{
	return GetBoundsToCheckIsEmptyOn();
}

TVoxelSharedPtr<FVoxelChunkMesh> FVoxelGreedyCubicMesher::CreateFullChunkImpl(FVoxelMesherTimes& Times)
{
	TArray<FVoxelMesherVertex> Vertices;
	TArray<uint32> Indices;

	TArray<FColor> TextureData;
	TArray<FVoxelIntBox> CollisionCubes;
	CreateGeometryTemplate(Times, Indices, Vertices, &TextureData, &CollisionCubes);

	TArray<FBox> ActualCollisionCubes;
	if (CollisionCubes.Num() > 0)
    {
		VOXEL_ASYNC_SCOPE_COUNTER("Build ActualCollisionCubes");

        ActualCollisionCubes.Reserve(CollisionCubes.Num());
        for (auto& Cube : CollisionCubes)
        {
			// Shift as cubic cubes are shifted
            ActualCollisionCubes.Add(Cube.Scale(Step).ToFBox().ShiftBy(FVector(-0.5f)));
        }
    }

	UnlockData();
	
	return MESHER_TIME_INLINE(CreateChunk, FVoxelMesherUtilities::CreateChunkFromVertices(
		Settings,
		LOD,
		MoveTemp(Indices),
		MoveTemp(Vertices),
		&TextureData,
		&ActualCollisionCubes));
}


void FVoxelGreedyCubicMesher::CreateGeometryImpl(FVoxelMesherTimes& Times, TArray<uint32>& Indices, TArray<FVector>& Vertices)
{
	struct FVertex : FVector
	{
		FVertex() = default;
		FVertex(const FVoxelMesherVertex& Vertex)
			: FVector(Vertex.Position)
		{
		}
	};
	
	CreateGeometryTemplate(Times, Indices, reinterpret_cast<TArray<FVertex>&>(Vertices), nullptr, nullptr);

	UnlockData();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template<typename T>
void FVoxelGreedyCubicMesher::CreateGeometryTemplate(FVoxelMesherTimes& Times, TArray<uint32>& Indices, TArray<T>& Vertices, TArray<FColor>* TextureData, TArray<FVoxelIntBox>* CollisionCubes)
{
	if (TextureData)
	{
		Accelerator = MakeUnique<FVoxelConstDataAccelerator>(Data, GetBoundsToLock());
	}
	
	constexpr int32 NumVoxels = RENDER_CHUNK_SIZE * RENDER_CHUNK_SIZE * RENDER_CHUNK_SIZE;
	constexpr int32 NumVoxelsWithNeighbors = CUBIC_CHUNK_SIZE_WITH_NEIGHBORS * CUBIC_CHUNK_SIZE_WITH_NEIGHBORS * CUBIC_CHUNK_SIZE_WITH_NEIGHBORS;

	TVoxelStaticBitArray<NumVoxelsWithNeighbors> ValuesBitArray;
	{
		VOXEL_ASYNC_SCOPE_COUNTER("Query Data");
		
		TVoxelStaticArray<FVoxelValue, NumVoxelsWithNeighbors> Values;

		TVoxelQueryZone<FVoxelValue> QueryZone(GetBoundsToCheckIsEmptyOn(), FIntVector(CUBIC_CHUNK_SIZE_WITH_NEIGHBORS), LOD, Values);
		MESHER_TIME_INLINE_VALUES(NumVoxelsWithNeighbors, Data.Get<FVoxelValue>(QueryZone, LOD));

		for (int32 Index = 0; Index < NumVoxelsWithNeighbors; Index++)
		{
			ValuesBitArray.Set(Index, !Values[Index].IsEmpty());
		}
	}
	
	const auto GetValue = [&](int32 X, int32 Y, int32 Z)
	{
		checkVoxelSlow(-1 <= X && X < RENDER_CHUNK_SIZE + 1);
		checkVoxelSlow(-1 <= Y && Y < RENDER_CHUNK_SIZE + 1);
		checkVoxelSlow(-1 <= Z && Z < RENDER_CHUNK_SIZE + 1);
		return ValuesBitArray.Test((X + 1) + (Y + 1) * (RENDER_CHUNK_SIZE + 2) + (Z + 1) * (RENDER_CHUNK_SIZE + 2) * (RENDER_CHUNK_SIZE + 2));
	};

	TVoxelStaticArray<TVoxelStaticBitArray<NumVoxels>, 6> FacesBitArrays;
	FacesBitArrays.Memzero();
	
	{
		VOXEL_ASYNC_SCOPE_COUNTER("Find faces");

		const auto GetFaceIndex = [&](int32 X, int32 Y, int32 Z)
		{
			return X + Y * RENDER_CHUNK_SIZE + Z * RENDER_CHUNK_SIZE * RENDER_CHUNK_SIZE;
		};
		
		for (int32 Z = 0; Z < RENDER_CHUNK_SIZE; Z++)
		{
			for (int32 Y = 0; Y < RENDER_CHUNK_SIZE; Y++)
			{
				for (int32 X = 0; X < RENDER_CHUNK_SIZE; X++)
				{
					if (!GetValue(X, Y, Z))
					{
						continue;
					}

					if (!GetValue(X - 1, Y, Z)) FacesBitArrays[0].Set(GetFaceIndex(Y, Z, X), true);
					if (!GetValue(X + 1, Y, Z)) FacesBitArrays[1].Set(GetFaceIndex(Y, Z, X), true);
					if (!GetValue(X, Y - 1, Z)) FacesBitArrays[2].Set(GetFaceIndex(Z, X, Y), true);
					if (!GetValue(X, Y + 1, Z)) FacesBitArrays[3].Set(GetFaceIndex(Z, X, Y), true);
					if (!GetValue(X, Y, Z - 1)) FacesBitArrays[4].Set(GetFaceIndex(X, Y, Z), true);
					if (!GetValue(X, Y, Z + 1)) FacesBitArrays[5].Set(GetFaceIndex(X, Y, Z), true);
				}
			}
		}
	}

	for (int32 Direction = 0; Direction < 6; Direction++)
	{
		TArray<FCubicQuad, TFixedAllocator<NumVoxels>> Quads;
		GreedyMeshing2D<RENDER_CHUNK_SIZE>(FacesBitArrays[Direction], Quads);
		
		VOXEL_ASYNC_SCOPE_COUNTER("Add faces");
		for (auto& Quad : Quads)
		{
			AddFace(Times, Direction, Quad, Step, Indices, Vertices, TextureData);
		}
	}

	// If Indices is empty, then we don't need to create any collision for this chunk (it's entirely inside the surface)
	if (CollisionCubes && Settings.bSimpleCubicCollision && Indices.Num() > 0)
	{
		VOXEL_ASYNC_SCOPE_COUNTER("CollisionCubes");

		{
			TVoxelStaticBitArray<NumVoxels> BitArray;
			{
				VOXEL_ASYNC_SCOPE_COUNTER("Copy");
				for (int32 Z = 0; Z < RENDER_CHUNK_SIZE; Z++)
				{
					for (int32 Y = 0; Y < RENDER_CHUNK_SIZE; Y++)
					{
						for (int32 X = 0; X < RENDER_CHUNK_SIZE; X++)
						{
							BitArray.Set(X + RENDER_CHUNK_SIZE * Y + RENDER_CHUNK_SIZE * RENDER_CHUNK_SIZE * Z, GetValue(X, Y, Z));
						}
					}
				}
			}

			GreedyMeshing3D<RENDER_CHUNK_SIZE>(BitArray, *CollisionCubes);
		}

		{
		    VOXEL_ASYNC_SCOPE_COUNTER("Cull");
			
			// Remove all useless cubes that are completely inside the surface
			CollisionCubes->RemoveAllSwap([&](const FVoxelIntBox& Cube)
            {
#if VOXEL_DEBUG
                Cube.Iterate([&](int32 X, int32 Y, int32 Z)
                {
                    ensure(GetValue(X, Y, Z));
                });
#endif

                for (int32 X = Cube.Min.X; X < Cube.Max.X; X++)
                {
                    for (int32 Y = Cube.Min.Y; Y < Cube.Max.Y; Y++)
                    {
                        if (!GetValue(X, Y, Cube.Min.Z - 1) ||
                            !GetValue(X, Y, Cube.Max.Z))
                        {
                            return false;
                        }
                    }
                }

                for (int32 X = Cube.Min.X; X < Cube.Max.X; X++)
                {
                    for (int32 Z = Cube.Min.Z; Z < Cube.Max.Z; Z++)
                    {
                        if (!GetValue(X, Cube.Min.Y - 1, Z) ||
                            !GetValue(X, Cube.Max.Y, Z))
                        {
                            return false;
                        }
                    }
                }

                for (int32 Y = Cube.Min.Y; Y < Cube.Max.Y; Y++)
                {
                    for (int32 Z = Cube.Min.Z; Z < Cube.Max.Z; Z++)
                    {
                        if (!GetValue(Cube.Min.X - 1, Y, Z) ||
                            !GetValue(Cube.Max.X, Y, Z))
                        {
                            return false;
                        }
                    }
                }

				return true;
			});
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template<uint32 GridSize, typename Allocator>
FORCEINLINE void FVoxelGreedyCubicMesher::GreedyMeshing2D(TVoxelStaticBitArray<GridSize * GridSize * GridSize>& InFaces, TArray<FCubicQuad, Allocator>& OutQuads)
{
	VOXEL_ASYNC_FUNCTION_COUNTER();

	for (uint32 Layer = 0; Layer < GridSize; Layer++)
	{
		static_assert(((GridSize * GridSize) % TVoxelStaticBitArray<GridSize * GridSize * GridSize>::NumBitsPerWord) == 0, "");
        auto& Faces = reinterpret_cast<TVoxelStaticBitArray<GridSize * GridSize>&>(*(InFaces.GetData() + Layer * GridSize * GridSize / InFaces.NumBitsPerWord));
		
		const auto TestAndClear = [&](uint32 X, uint32 Y)
		{
			checkVoxelSlow(X < GridSize);
			checkVoxelSlow(Y < GridSize);
			
			return Faces.TestAndClear(X + Y * GridSize);
		};
		const auto TestAndClearLine = [&](uint32 X, uint32 Width, uint32 Y)
		{
			checkVoxelSlow(X + Width <= GridSize);
			checkVoxelSlow(Y < GridSize);
			
			return Faces.TestAndClearRange(X + Y * GridSize, Width);
		};
		
		for (uint32 X = 0; X < GridSize; X++)
		{
			const uint32 Mask = 1 << X;
			for (uint32 Y = 0; Y < GridSize;)
			{
				if (GridSize == 32)
				{
					// Simpler logic, as Y is the word index
					if (!(Faces.GetInternal(Y) & Mask))
					{
						Y++;
						continue;
					}
				}
				else
				{
					if (!Faces.Test(X + Y * GridSize))
					{
						Y++;
						continue;
					}
				}

				uint32 Width = 1;
				while (X + Width < GridSize && TestAndClear(X + Width, Y))
				{
					Width++;
				}

				uint32 Height = 1;
				while (Y + Height < GridSize && TestAndClearLine(X, Width, Y + Height))
				{
					Height++;
				}

				OutQuads.Add(FCubicQuad{ Layer, X, Y, Width, Height });

				Y += Height;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template<uint32 GridSize, typename Allocator>
void FVoxelGreedyCubicMesher::GreedyMeshing3D(TVoxelStaticBitArray<GridSize * GridSize * GridSize>& Data, TArray<FVoxelIntBox, Allocator>& OutCubes)
{
	VOXEL_ASYNC_FUNCTION_COUNTER();

	const auto TestAndClear = [&](uint32 X, uint32 Y, uint32 Z)
	{
		checkVoxelSlow(X < GridSize);
		checkVoxelSlow(Y < GridSize);
		checkVoxelSlow(Z < GridSize);
		
		return Data.TestAndClear(X + Y * GridSize + Z * GridSize * GridSize);
	};
	const auto TestAndClearLine = [&](uint32 X, uint32 SizeX, uint32 Y, uint32 Z)
	{
		checkVoxelSlow(X + SizeX <= GridSize);
		checkVoxelSlow(Y < GridSize);
		checkVoxelSlow(Z < GridSize);
		
		return Data.TestAndClearRange(X + Y * GridSize + Z * GridSize * GridSize, SizeX);
	};
	const auto TestAndClearBlock = [&](uint32 X, uint32 SizeX, uint32 Y, uint32 SizeY, uint32 Z)
	{
		checkVoxelSlow(X + SizeX <= GridSize);
		checkVoxelSlow(Y + SizeY <= GridSize);
		checkVoxelSlow(Z < GridSize);

		for (uint32 Index = 0; Index < SizeY; Index++)
		{
			if (!Data.TestRange(X + (Y + Index) * GridSize + Z * GridSize * GridSize, SizeX))
			{
				return false;
			}
		}
		for (uint32 Index = 0; Index < SizeY; Index++)
		{
			Data.SetRange(X + (Y + Index) * GridSize + Z * GridSize * GridSize, SizeX, false);
		}
		return true;
	};
	
	for (uint32 X = 0; X < GridSize; X++)
	{
		for (uint32 Y = 0; Y < GridSize; Y++)
		{
			for (uint32 Z = 0; Z < GridSize;)
			{
				if (!Data.Test(X + Y * GridSize + Z * GridSize * GridSize))
				{
					Z++;
					continue;
				}

				uint32 SizeX = 1;
				while (X + SizeX < GridSize && TestAndClear(X + SizeX, Y, Z))
				{
					SizeX++;
				}

				uint32 SizeY = 1;
				while (Y + SizeY < GridSize && TestAndClearLine(X, SizeX, Y + SizeY, Z))
				{
					SizeY++;
				}

				uint32 SizeZ = 1;
				while (Z + SizeZ < GridSize && TestAndClearBlock(X, SizeX, Y, SizeY, Z + SizeZ))
				{
					SizeZ++;
				}

				const auto Min = FIntVector(X, Y, Z);
				const auto Max = Min + FIntVector(SizeX, SizeY, SizeZ);
				OutCubes.Add(FVoxelIntBox(Min, Max));

				Z += SizeZ;
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

template<typename T>
FORCEINLINE void FVoxelGreedyCubicMesher::AddFace(
	FVoxelMesherTimes& Times,
	int32 Direction,
	const FCubicQuad& Quad, 
	int32 Step,
	TArray<uint32>& Indices,
	TArray<T>& Vertices,
	TArray<FColor>* TextureData)
{
	const int32 ZAxis = Direction / 2;
	const bool bInverted = Direction & 0x1;

	const int32 XAxis = (ZAxis + 1) % 3;
	const int32 YAxis = (ZAxis + 2) % 3;
	
	/**
	 * 1 --- 2
	 * |  /  |
	 * 0 --- 3
	 * 
	 * Triangles: 0 1 2, 0 2 3
	 */

	if (bInverted)
	{
		Indices.Add(Vertices.Num() + 2);
		Indices.Add(Vertices.Num() + 1);
		Indices.Add(Vertices.Num() + 0);

		Indices.Add(Vertices.Num() + 3);
		Indices.Add(Vertices.Num() + 2);
		Indices.Add(Vertices.Num() + 0);
	}
	else
	{
		Indices.Add(Vertices.Num() + 0);
		Indices.Add(Vertices.Num() + 1);
		Indices.Add(Vertices.Num() + 2);

		Indices.Add(Vertices.Num() + 0);
		Indices.Add(Vertices.Num() + 2);
		Indices.Add(Vertices.Num() + 3);
	}

	FVoxelMesherVertex Vertex;
	Vertex.Material = FVoxelMaterial::Default();

	if (TextureData)
	{
		const auto GetMaterial = [&](int32 X, int32 Y)
		{
			FIntVector Position;
			Position[XAxis] = Quad.StartX + X;
			Position[YAxis] = Quad.StartY + Y;
			Position[ZAxis] = Quad.Layer;

			Position = Position * Step + ChunkPosition;

			return MESHER_TIME_INLINE_MATERIALS(1, Accelerator->GetMaterial(Position, LOD));
		};
		
		if (Quad.SizeX == 1 && Quad.SizeY == 1)
		{
			Vertex.Material = GetMaterial(0, 0);
			Vertex.Material.CubicColor_SetUseTextureFalse();
		}
		else
		{
			// TODO don't allocate texture data if all colors are the same
			bool bMaterialSet = false;
			for (uint32 Y = 0; Y < Quad.SizeY; Y++)
			{
				for (uint32 X = 0; X < Quad.SizeX; X++)
				{
					const FVoxelMaterial Material = GetMaterial(X, Y);

					if (!bMaterialSet)
					{
						bMaterialSet = true;
						Vertex.Material = Material;
						Vertex.Material.CubicColor_SetQuadWidth(Quad.SizeX);
						Vertex.Material.CubicColor_SetTextureDataIndex(TextureData->Num());
					}

					TextureData->Add(Material.GetColor());
				}
			}
		}
	}

	Vertex.Normal = FVector{ ForceInit };
	Vertex.Normal[ZAxis] = bInverted ? 1 : -1;
	
	Vertex.Tangent.TangentX = FVector{ ForceInit };
	Vertex.Tangent.TangentX[XAxis] = 1;

	const auto SetPosition = [&](int32 X, int32 Y)
	{
		Vertex.TextureCoordinate.X = Quad.SizeX * X;
		Vertex.TextureCoordinate.Y = Quad.SizeY * Y;
		Vertex.Position[XAxis] = Quad.StartX + Quad.SizeX * X;
		Vertex.Position[YAxis] = Quad.StartY + Quad.SizeY * Y;
		Vertex.Position[ZAxis] = Quad.Layer + bInverted;
		Vertex.Position = Step * Vertex.Position - 0.5f;
	};
	
	SetPosition(0, 0);
	Vertices.Add(T(Vertex));

	SetPosition(1, 0);
	Vertices.Add(T(Vertex));
	
	SetPosition(1, 1);
	Vertices.Add(T(Vertex));

	SetPosition(0, 1);
	Vertices.Add(T(Vertex));
}
