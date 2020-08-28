// Copyright 2020 Phyronnaz

#pragma once

#include "CoreMinimal.h"
#include "VoxelIntBox.h"
#include "Templates/SubclassOf.h"
#include "VoxelTools/VoxelPaintMaterial.h"
#include "VoxelTool.generated.h"

class AVoxelWorld;
class UStaticMesh;
struct FHitResult;

struct FVoxelToolKeys
{
	static constexpr const TCHAR* AlternativeMode = TEXT("AlternativeMode");
};

struct FVoxelToolAxes
{
	static constexpr const TCHAR* BrushSize = TEXT("BrushSize");
	static constexpr const TCHAR* Falloff = TEXT("Falloff");
	static constexpr const TCHAR* Strength = TEXT("Strength");
};

USTRUCT(BlueprintType)
struct FVoxelToolTickData
{
	GENERATED_BODY()

	UPROPERTY(Category = "Voxel", EditAnywhere, BlueprintReadWrite)
	FVector2D MousePosition = FVector2D(-1, -1);
	
	UPROPERTY(Category = "Voxel", EditAnywhere, BlueprintReadWrite)
	FVector CameraViewDirection = FVector::ForwardVector;

	UPROPERTY(Category = "Voxel", EditAnywhere, BlueprintReadWrite)
	bool bEdit = false;

	UPROPERTY(Category = "Voxel", EditAnywhere, BlueprintReadWrite)
	TMap<FName, bool> Keys;

	UPROPERTY(Category = "Voxel", EditAnywhere, BlueprintReadWrite)
	TMap<FName, float> Axes;

public:
	bool IsKeyDown(FName Key) const
	{
		return Keys.FindRef(Key);
	}
	float GetAxis(FName Axis) const
	{
		return Axes.FindRef(Axis);
	}
	
	bool IsAlternativeMode() const
	{
		return IsKeyDown(FVoxelToolKeys::AlternativeMode);
	}

public:
	bool Deproject(const FVector2D& InScreenPosition, FVector& OutWorldPosition, FVector& OutWorldDirection) const
	{
		return ensure(DeprojectLambda) && DeprojectLambda(InScreenPosition, OutWorldPosition, OutWorldDirection);
	}
	const FVector& GetRayOrigin() const { return RayOrigin; }
	const FVector& GetRayDirection() const { return RayDirection; }

public:
	using FDeproject = TFunction<bool(const FVector2D& /*InScreenPosition*/, FVector& /*OutWorldPosition*/, FVector& /*OutWorldDirection*/)>;
	
	void Init(const FDeproject& InDeprojectLambda)
	{
		//ensure(MousePosition.X >= 0 && MousePosition.Y >= 0);
		DeprojectLambda = InDeprojectLambda;
		Deproject(MousePosition, RayOrigin, RayDirection);
	}

private:
	FDeproject DeprojectLambda;
	FVector RayOrigin = FVector::ZeroVector;
	FVector RayDirection = FVector::ForwardVector;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FVoxelTool_OnBoundsUpdated, AVoxelWorld*, World, FVoxelIntBox, Bounds);

UCLASS(BlueprintType)
class VOXEL_API UVoxelToolSharedConfig : public UObject
{
	GENERATED_BODY()

public:
	UVoxelToolSharedConfig();
	
public:
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, meta = (ClampMin = 0, UIMin = 0, UIMax = 20000))
	float BrushSize = 1000;
	
	UPROPERTY(Category = "Shared Config - Paint", EditAnywhere, BlueprintReadWrite, meta = (ShowOnlyInnerProperties, PaintMaterial))
	FVoxelPaintMaterial PaintMaterial;

public:
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (UIMin = 0, UIMax = 1))
	float ToolOpacity = 0.5f;
	
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (UIMin = 0, UIMax = 1))
	float AlignToMovementSmoothness = 0.75;

	// Input speed: 0.05 increase radius by 5% every time you press ]
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay, meta = (UIMin = 0, UIMax = 1))
	float ControlSpeed = 0.05f;

	// If empty, allow editing all worlds
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Transient, meta = (HideInPanel))
	TArray<AVoxelWorld*> WorldsToEdit;
	
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay)
	bool bCacheData = true;

	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay)
	bool bMultiThreaded = true;
	
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay)
	bool bRegenerateSpawners = true;
	
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay)
	bool bCheckForSingleValues = true;
	
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay)
	bool bWaitForUpdates = true;
	
	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, AdvancedDisplay)
	bool bDebug = false;

	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, meta = (HideInPanel))
	UStaticMesh* PlaneMesh = nullptr;

	UPROPERTY(Category = "Shared Config", EditAnywhere, BlueprintReadWrite, meta = (HideInPanel))
	UMaterialInterface* PlaneMaterial = nullptr;

public:
	UPROPERTY(BlueprintAssignable)
	FVoxelTool_OnBoundsUpdated OnBoundsUpdated;

public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FRegisterTransactionDelegate, FName, AVoxelWorld*);
	FRegisterTransactionDelegate RegisterTransaction;

#if WITH_EDITOR
	FSimpleMulticastDelegate RefreshDetails;
#endif
};

UCLASS(BlueprintType, Abstract)
class VOXEL_API UVoxelTool : public UObject
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, Category = "Config")
	FName ToolName;

	UPROPERTY(EditDefaultsOnly, Category = "Config")
	FText ToolTip;
	
	UPROPERTY(EditDefaultsOnly, Category = "Config")
	bool bShowInDropdown = true;

	UPROPERTY(EditDefaultsOnly, Category = "Config")
	bool bShowPaintMaterial = false;

public:
	// Shared config allows to share some values across several tools, like the brush size or the paint material
	// If not set, it will be created in EnableTool
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Voxel")
	UVoxelToolSharedConfig* SharedConfig = nullptr;
	
public:
	// Call this to do some initial setup
	// Will be called in the first tool tick if you don't
	UFUNCTION(BlueprintCallable, Category = "Voxel|Tools")
	virtual void EnableTool();

	// Call this to delete any tool preview actors
	UFUNCTION(BlueprintCallable, Category = "Voxel|Tools")
	virtual void DisableTool();

public:
	UFUNCTION(BlueprintCallable, Category = "Voxel|Tools")
	virtual AVoxelWorld* GetVoxelWorld() const { ensure(false); return nullptr; }
	
public:
	UFUNCTION(BlueprintCallable, Category = "Voxel|Tools")
	void AdvancedTick(UWorld* World, const FVoxelToolTickData& TickData);

	/**
	 * Tick the tool
	 * @param	PlayerController	The player controller - use GetPlayerController to get it
	 * @param	bEdit				Whether the user is pressing the edit button this frame
	 * @param	Keys				The keys pressed this frame. Use MakeToolKeys. You can add additional values to the map if you have custom tools using them.
	 * @param	Axes				The axes values in this frame, to control brush size/strength etc. Use MakeToolAxes. You can add additional values to the map if you have custom tools using them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Tools", meta = (AutoCreateRefTerm = "Keys, Axes"))
	void SimpleTick(
		APlayerController* PlayerController, 
		bool bEdit, 
		const TMap<FName, bool>& Keys, 
		const TMap<FName, float>& Axes);
	
	UFUNCTION(BlueprintCallable, Category = "Voxel|Tools", meta = (AutoCreateRefTerm = "Keys, Axes", DefaultToSelf = "World"))
	void Apply(
		AVoxelWorld* World,
		FVector Position,
		FVector Normal,
		const TMap<FName, bool>& Keys, 
		const TMap<FName, float>& Axes);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Tools")
	FName GetToolName() const;

public:
	UFUNCTION(BlueprintPure, Category = "Voxel|Tools")
	static TMap<FName, bool> MakeToolKeys(bool bAlternativeMode);

	UFUNCTION(BlueprintPure, Category = "Voxel|Tools")
	static TMap<FName, float> MakeToolAxes(float BrushSizeDelta, float FalloffDelta, float StrengthDelta);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Tools", meta = (DeterminesOutputType = "ToolClass"))
	static UVoxelTool* MakeVoxelTool(TSubclassOf<UVoxelTool> ToolClass);

public:
	UFUNCTION(BlueprintPure, Category = "Voxel|Tools|Tick Data")
	static bool IsKeyDown(FVoxelToolTickData TickData, FName Key)
	{
		return TickData.IsKeyDown(Key);
	}
	UFUNCTION(BlueprintPure, Category = "Voxel|Tools|Tick Data")
	static float GetAxis(FVoxelToolTickData TickData, FName Axis)
	{
		return TickData.GetAxis(Axis);
	}
	UFUNCTION(BlueprintPure, Category = "Voxel|Tools|Tick Data")
	static bool IsAlternativeMode(FVoxelToolTickData TickData)
	{
		return TickData.IsAlternativeMode();
	}
	UFUNCTION(BlueprintPure, Category = "Voxel|Tools|Tick Data")
	static bool Deproject(FVoxelToolTickData TickData, FVector2D ScreenPosition, FVector& WorldPosition, FVector& WorldDirection)
	{
		return TickData.Deproject(ScreenPosition, WorldPosition, WorldDirection);
	}
	UFUNCTION(BlueprintPure, Category = "Voxel|Tools|Tick Data")
	static FVector GetRayOrigin(FVoxelToolTickData TickData)
	{
		return TickData.GetRayOrigin();
	}
	UFUNCTION(BlueprintPure, Category = "Voxel|Tools|Tick Data")
	static FVector GetRayDirection(FVoxelToolTickData TickData)
	{
		return TickData.GetRayDirection();
	}
	
protected:
	enum class ECallToolMode
	{
		Tick,
		Apply
	};
	struct FCallToolParameters
	{
		ECallToolMode Mode;
		FVector Position;
		FVector Normal;
		bool bBlockingHit = false;
	};
	virtual void CallTool(AVoxelWorld* VoxelWorld, const FVoxelToolTickData& TickData, const FCallToolParameters& Parameters) {}

public:
	//~ Begin UObject Interface
	virtual void BeginDestroy() override;
	//~ End UObject Interface
	
private:
	UPROPERTY(Transient)
	bool bEnabled = false;
	
	// For debug
	UPROPERTY(Transient)
	FVoxelToolTickData FrozenTickData;
};