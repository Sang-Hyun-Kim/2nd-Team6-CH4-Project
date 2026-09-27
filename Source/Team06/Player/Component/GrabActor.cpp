// GrabActor.cpp

#include "Player/Component/GrabActor.h"
#include "Team06.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"

AGrabActor::AGrabActor()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bIsActive = false;

    if (!RootComponent)
    {
        RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootComponent"));
    }

    RangeIndicator = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RangeIndicator"));
    RangeIndicator->SetupAttachment(RootComponent);
    RangeIndicator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RangeIndicator->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(TEXT("StaticMesh'/Engine/BasicShapes/Sphere.Sphere'"));
    if (SphereMeshAsset.Succeeded())
    {
        RangeIndicator->SetStaticMesh(SphereMeshAsset.Object);
    }
}

void AGrabActor::BeginPlay()
{
    Super::BeginPlay();

    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

    ConstraintComp = nullptr;

    if (RangeIndicator && RangeIndicatorBaseMaterial)
    {
        RangeIndicatorMaterial = UMaterialInstanceDynamic::Create(RangeIndicatorBaseMaterial, this);
        RangeIndicator->SetMaterial(0, RangeIndicatorMaterial);
    }
}

void AGrabActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!HasAuthority() || !bIsActive)
    {
        if (RangeIndicator)
        {
            RangeIndicator->SetVisibility(false);
        }
        return;
    }

    ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
    if (!OwnerChar) return;

    USkeletalMeshComponent* Mesh = OwnerChar->GetMesh();
    if (!Mesh) return;

    FVector HandLocation = Mesh->GetSocketLocation(TEXT("hand_l_socket"));

    if (RangeIndicator)
    {
        RangeIndicator->SetWorldLocation(HandLocation);
        float ScaleFactor = TraceRadius / 50.f;
        RangeIndicator->SetWorldScale3D(FVector(ScaleFactor));
        RangeIndicator->SetVisibility(true);
    }

    FHitResult Hit;
    FVector Start = HandLocation;
    FVector End = HandLocation + FVector(0.f, 0.f, -40.f);

    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(GetOwner());

    bool bHit = UKismetSystemLibrary::SphereTraceSingle(
        this,
        Start,
        End,
        TraceRadius,
        UEngineTypes::ConvertToTraceType(ECC_Visibility),
        false,
        IgnoreActors,
        CVarDebugGeneral.GetValueOnGameThread() ? EDrawDebugTrace::ForOneFrame : EDrawDebugTrace::None, // [debug]
        Hit,
        true,
        FLinearColor::Red,
        FLinearColor::Green,
        5.0f
    );

    FLinearColor NewColor = bHit ? FLinearColor::Green : FLinearColor::Red;
    if (RangeIndicatorMaterial)
    {
        RangeIndicatorMaterial->SetVectorParameterValue(TEXT("BaseColor"), NewColor);
    }

    if (bHit && !ConstraintComp)
    {
        TryGrabBone(HandLocation);
    }
}

void AGrabActor::ActivateGrab()
{
    bIsActive = true;
}

void AGrabActor::ReleaseGrab()
{
    if (!HasAuthority()) return;

    if (ConstraintComp)
    {
        ConstraintComp->BreakConstraint();
        ConstraintComp->DestroyComponent();
        ConstraintComp = nullptr;
        UE_LOG(LogTemp, Warning, TEXT("ReleaseGrab: Constraint 해제 완료"));
    }

    if (LastTargetMesh)
    {
        LastTargetMesh->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), false, true);
        LastTargetMesh->SetCollisionProfileName(TEXT("CharacterMesh"));
        LastTargetMesh = nullptr;
    }

    bIsActive = false;
    if (RangeIndicator)
    {
        RangeIndicator->SetVisibility(false);
    }
}

void AGrabActor::TryGrabBone(const FVector& HandLocation)
{
    if (!HasAuthority()) return;

    MulticastTryGrabBone(HandLocation);
}

void AGrabActor::MulticastTryGrabBone_Implementation(const FVector& HandLocation)
{
    // 이미 제약이 있으면 더 이상 시도 안 함
    if (ConstraintComp) return;

    // Sphere Trace로 대상 검사
    FHitResult Hit;
    TArray<AActor*> IgnoreActors;
    IgnoreActors.Add(GetOwner());

    bool bHit = UKismetSystemLibrary::SphereTraceSingle(
        this,                                              // WorldContextObject
        HandLocation,                                      // Start
        HandLocation + FVector(0.f, 0.f, -40.f),             // End
        TraceRadius,                                       // Radius
        UEngineTypes::ConvertToTraceType(ECC_Visibility),  // TraceChannel
        false,                                             // bTraceComplex
        IgnoreActors,                                      // ActorsToIgnore
        EDrawDebugTrace::None,                             // DrawDebugType
        Hit,                                               // OutHit
        true                                               // bIgnoreSelf
    );

    if (!bHit) return;

    // 타겟 메시/본 가져오기
    USkeletalMeshComponent* TargetMesh = Cast<USkeletalMeshComponent>(Hit.GetComponent());
    if (!TargetMesh) return;
    ACharacter* TargetChar = Cast<ACharacter>(Hit.GetActor());
    if (!TargetChar) return;

    // 타겟 메시를 Ragdoll 모드로 전환
    TargetMesh->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true, true);
    TargetMesh->SetCollisionProfileName(TEXT("Ragdoll"));

    FName ClosestBone = TargetMesh->FindClosestBone(HandLocation);
    USkeletalMeshComponent* OwnerMesh = Cast<ACharacter>(GetOwner())->GetMesh();
    if (!OwnerMesh) return;

    // 제약 컴포넌트 생성
    LastTargetMesh = TargetMesh;
    ConstraintComp = NewObject<UPhysicsConstraintComponent>(this);
    ConstraintComp->RegisterComponent();

    // **핵심 수정**: RootComponent가 아니라 플레이어 메시의 hand_l 본에 부착
    ConstraintComp->AttachToComponent(
        OwnerMesh,
        FAttachmentTransformRules::KeepWorldTransform,
        TEXT("hand_l")
    );
    // 로컬 위치(0,0,0)이 손 위치와 일치하도록 설정
    ConstraintComp->SetRelativeLocation(FVector::ZeroVector);

    // 제약 양쪽 컴포넌트/본 설정
    ConstraintComp->SetConstrainedComponents(
        OwnerMesh, TEXT("hand_l"),
        TargetMesh, ClosestBone
    );

    // 선형 이동 전부 고정
    ConstraintComp->ConstraintInstance.SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
    ConstraintComp->ConstraintInstance.SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
    ConstraintComp->ConstraintInstance.SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.f);

    ConstraintComp->ConstraintInstance.SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Locked, 0.f);
    ConstraintComp->ConstraintInstance.SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Locked, 0.f);
    ConstraintComp->ConstraintInstance.SetAngularTwistLimit(EAngularConstraintMotion::ACM_Locked, 0.f);

    UE_LOG(LogTemp, Warning,
        TEXT("TryGrabBone: 제약 연결 완료 - 내 손과 타겟 본 [%s] 연결됨"),
        *ClosestBone.ToString()
    );
}

void AGrabActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AGrabActor, ConstraintComp);
}