#include "PlayerCharacter.h"
#include "Team06.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/DamageType.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/GameStateBase.h"
#include "Player/Controller/PCController_GamePlay.h"
#include "Player/Component/ItemManagerComponent.h"
#include "Player/Component/EquipItemMeshActor.h" 
#include "Player/Component/GrabActor.h" 
#include "Components/SphereComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "System/GameSystem/T6GameStateBase_GameLevel.h"

APlayerCharacter::APlayerCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;

    GetCharacterMovement()->bUseControllerDesiredRotation = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);

    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->TargetArmLength = 400.f;
    SpringArm->bUsePawnControlRotation = true;
    SpringArm->SetupAttachment(GetRootComponent());
    SpringArm->bDoCollisionTest = false;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->bUsePawnControlRotation = false;
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);

    GrabSphere = CreateDefaultSubobject<USphereComponent>(TEXT("GrabSphere"));
    GrabSphere->SetupAttachment(GetMesh(), TEXT("hand_l_socket"));
    GrabSphere->InitSphereRadius(100.f);
    GrabSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GrabSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
    GrabSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

    HandConstraint = CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("HandConstraint"));
    if (HandConstraint)
    {
        HandConstraint->SetupAttachment(GetMesh(), TEXT("hand_l_socket"));
    }
}

void APlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (const AT6GameStateBase_GameLevel* GS = GetWorld()->GetGameState<AT6GameStateBase_GameLevel>())
    {
        if (MatchState != GS->MatchState)
        {
            MatchState = GS->MatchState;
        }
    }
}

void APlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

    EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &APlayerCharacter::HandleMoveInput);
    EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &APlayerCharacter::HandleLookInput);
    EIC->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ACharacter::Jump);
    EIC->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
    EIC->BindAction(LeftHandAttackAction, ETriggerEvent::Triggered, this, &APlayerCharacter::HandleLeftHandMeleeAttack);
    EIC->BindAction(RightHandAttackAction, ETriggerEvent::Triggered, this, &APlayerCharacter::HandleRightHandMeleeAttack);
    EIC->BindAction(BKeyAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleBKey);
    EIC->BindAction(ESCKeyAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleESCKey);
    EIC->BindAction(FKeyAction, ETriggerEvent::Triggered, this, &APlayerCharacter::HandleFKey);

    EIC->BindAction(GrabAction, ETriggerEvent::Triggered, this, &APlayerCharacter::HandleGrabKey);
    EIC->BindAction(GrabAction, ETriggerEvent::Completed, this, &APlayerCharacter::HandleGrabKey);

    EIC->BindAction(CheatKeyAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleCheatKey);
    EIC->BindAction(Cheat2KeyAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleCheat2Key);
    EIC->BindAction(Cheat3KeyAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleCheat3Key);
    EIC->BindAction(Cheat4KeyAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleCheat4Key);
    EIC->BindAction(Cheat5KeyAction, ETriggerEvent::Started, this, &APlayerCharacter::HandleCheat5Key);
}

void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    PlayResultMontage();

    if (HasAuthority() == false && IsLocallyControlled() == true)
    {
        APlayerController* PC = Cast<APlayerController>(GetController());
        checkf(IsValid(PC), TEXT("PlayerController is invalid."));

        UEnhancedInputLocalPlayerSubsystem* EILPS = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
        checkf(IsValid(EILPS), TEXT("EnhancedInputLocalPlayerSubsystem is invalid."));

        EILPS->AddMappingContext(InputMappingContext, 0);
    }

    if (HasAuthority())
    {
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.Instigator = this;
        GrabActorInstance = GetWorld()->SpawnActor<AGrabActor>(
            GrabActorClass,
            GetActorLocation(),
            GetActorRotation(),
            Params
        );
    }

    ValidateEssentialReferences();
    UpdateStatsFromDataTable();
    MatchState = EMatchState::Playing;
}

void APlayerCharacter::ValidateEssentialReferences()
{
    Super::ValidateEssentialReferences();


    if (!ItemManager)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ItemManagerComponent 확인"));
    }
    if (!ItemManager || !ItemManager->ItemDataTable)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ItemDataTable 확인"));
    }
    if (!ItemRightMeleeAttackMontage)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ItemRightMeleeAttackMontage 확인"));
    }
    if (!ItemRightRangedAttackMontage)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ItemRightRangedAttackMontage 확인"));
    }
    if (!RightMeleeAttackMontage)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : RightMeleeAttackMontage 확인"));
    }
    if (!LeftMeleeAttackMontage)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : LeftMeleeAttackMontage 확인"));
    }
    if (!ItemShortRangedAttackMontage)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ItemShortRangedAttackMontage 확인"));
    }
    if (!InputMappingContext)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : InputMappingContext 확인"));
    }
    if (!MoveAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : MoveAction 확인"));
    }
    if (!LookAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : LookAction 확인"));
    }
    if (!JumpAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : JumpAction 확인"));
    }
    if (!LeftHandAttackAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : LeftHandAttackAction 확인"));
    }
    if (!RightHandAttackAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : RightHandAttackAction 확인"));
    }
    if (!BKeyAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : BKeyAction 확인"));
    }
    if (!ESCKeyAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ESCKeyAction 확인"));
    }
    if (!FKeyAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : FKeyAction 확인"));
    }
    if (!CheatKeyAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : CheatKeyAction 확인"));
    }
    if (!Cheat2KeyAction)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : Cheat2KeyAction 확인"));
    }
}

void APlayerCharacter::HandleMoveInput(const FInputActionValue& InValue)
{
    if (!Controller)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : Controller 확인"));
        return;
    }

    const FVector2D InMovementVector = InValue.Get<FVector2D>();

    const FRotator ControlRotation = Controller->GetControlRotation();
    const FRotator ControlYawRotation(0.0f, ControlRotation.Yaw, 0.0f);

    const FVector ForwardDirection = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::X);
    const FVector RightDirection = FRotationMatrix(ControlYawRotation).GetUnitAxis(EAxis::Y);

    AddMovementInput(ForwardDirection, InMovementVector.X);
    AddMovementInput(RightDirection, InMovementVector.Y);
}

void APlayerCharacter::HandleLookInput(const FInputActionValue& InValue)
{
    if (!IsValid(Controller))
    {
        UE_LOG(LogTemp, Error, TEXT("Controller is invalid."));
        return;
    }

    const FVector2D InLookVector = InValue.Get<FVector2D>();

    AddControllerYawInput(InLookVector.X);
    AddControllerPitchInput(InLookVector.Y);
}

///////////////////////////////////////////
//           MeleeAttack                 //
///////////////////////////////////////////

#pragma region Attack
void APlayerCharacter::HandleLeftHandMeleeAttack(const FInputActionValue& InValue)
{
    if (MatchState != EMatchState::Playing)
    {
        return;
    }

    if (!bLeftCanAttack || GetbIsStunned())
    {
        return;
    }

    float StartAttackTime = GetWorld()->GetTimeSeconds();
    bLeftCanAttack = false;
    ServerRPCLeftHandMeleeAttack(StartAttackTime);
}

void APlayerCharacter::HandleRightHandMeleeAttack(const FInputActionValue& InValue)
{
    if (MatchState != EMatchState::Playing)
    {
        return;
    }

    if (!bRightCanAttack || GetbIsStunned())
    {
        return;
    }

    const FName EquippedItem = CurrentEquippedItemName;
    float StartAttackTime = GetWorld()->GetTimeSeconds();
    bRightCanAttack = false;

    if (EquippedItem == "DEFAULT")
    {
        ServerRPCRightHandMeleeAttack(StartAttackTime);
    }
    else
    {
        static const FString Context = TEXT("EquipItemContext");
        if (const UDataTable* Table = ItemManager->ItemDataTable)
        {
            if (const FEquipItemDataRow* Row = Table->FindRow<FEquipItemDataRow>(EquippedItem, Context))
            {
                switch (Row->ItemType)
                {
                case EEquipItemType::Melee:
                    ServerRPCItemMeleeAttack(StartAttackTime);
                    break;
                case EEquipItemType::Ranged:
                    ServerRPCItemRangedAttack(StartAttackTime);
                    break;
                default:
                    UE_LOG(LogTemp, Warning, TEXT("Invalid item type: %s"), *EquippedItem.ToString());
                    break;
                }
            }
        }
    }
}

void APlayerCharacter::ServerRPCItemMeleeAttack_Implementation(float InStartTime)
{
    PendingAttackOffset = GetActorRightVector() * 50.0f;
    PerformMeleeAttack(PendingAttackOffset, ItemRightMeleeAttackMontage, false);
    MulticastPlayMeleeAttackMontage(ItemRightMeleeAttackMontage);
    //ServerSetEquippedItemName("DEFAULT");
}

bool APlayerCharacter::ServerRPCItemMeleeAttack_Validate(float InStartTime)
{
    return true;
}

void APlayerCharacter::ServerRPCItemRangedAttack_Implementation(float InStartTime)
{
    bRightCanAttack = false;
    MulticastPlayMeleeAttackMontage(ItemRightRangedAttackMontage);

    float Duration = ItemRightRangedAttackMontage ? ItemRightRangedAttackMontage->GetPlayLength() : 1.0f;
    GetWorldTimerManager().SetTimer(
        AttackTimerHandle,
        [this]()
        {
            ResetRightAttack();
        },
        Duration,
        false
    );
}

bool APlayerCharacter::ServerRPCItemRangedAttack_Validate(float InStartTime)
{
    return true;
}

void APlayerCharacter::ServerStartShortThrow_Implementation()
{
    MulticastPlayMeleeAttackMontage(ItemShortRangedAttackMontage);
    bRightCanAttack = false;

    float Duration = ItemShortRangedAttackMontage ? ItemShortRangedAttackMontage->GetPlayLength() : 1.0f;
    GetWorldTimerManager().SetTimer(
        AttackTimerHandle,
        [this]()
        {
            ResetRightAttack();
            MulticastResetAttack(false);
        },
        Duration,
        false
    );
}


void APlayerCharacter::PerformMeleeAttack(const FVector& Offset, UAnimMontage* Montage, bool bIsLeftHand)
{
    UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
    if (AnimInstance && Montage)
    {
        AnimInstance->Montage_Play(Montage);

        if (bIsLeftHand)
        {
            bLeftCanAttack = false;
        }
        else
        {
            bRightCanAttack = false;
        }

        float Duration = Montage->GetPlayLength();
        GetWorld()->GetTimerManager().SetTimer(
            bIsLeftHand ? LeftAttackTimerHandle : AttackTimerHandle,
            [this, bIsLeftHand]()
            {
                if (bIsLeftHand)
                {
                    bLeftCanAttack = true;
                }
                else
                {
                    bRightCanAttack = true;
                }
            },
            Duration,
            false
        );
    }
    else
    {
        if (bIsLeftHand)
        {
            bLeftCanAttack = true;
        }
        else
        {
            bRightCanAttack = true;
        }
    }
}

void APlayerCharacter::CheckMeleeAttackHit(const FVector& AttackOffset)
{
    TArray<FHitResult> OutHitResults;
    FCollisionQueryParams Params(NAME_None, false, this);

    const float MeleeAttackRange = 50.f;
    const float MeleeAttackRadius = 50.f;
    const FVector Forward = GetActorForwardVector();
    const FVector Start = GetActorLocation() + AttackOffset + Forward * GetCapsuleComponent()->GetScaledCapsuleRadius();
    const FVector End = Start + Forward * MeleeAttackRange;

    bool bHit = GetWorld()->SweepMultiByChannel(
        OutHitResults,
        Start,
        End,
        FQuat::Identity,
        ECC_Camera,
        FCollisionShape::MakeSphere(MeleeAttackRadius),
        Params
    );
    TSet<APlayerBase*> ProcessedPlayers;

    for (const FHitResult& Hit : OutHitResults)
    {
        AActor* HitActor = Hit.GetActor();
        if (HitActor && HitActor != this)
        {
            if (APlayerBase* DamagedPlayer = Cast<APlayerBase>(HitActor))
            {
                if (ProcessedPlayers.Contains(DamagedPlayer))
                {
                    continue;
                }
                ProcessedPlayers.Add(DamagedPlayer);

                ServerRPCPerformMeleeHit(DamagedPlayer, GetWorld()->GetGameState()->GetServerWorldTimeSeconds());
            }
        }
    }

    FColor DrawColor = bHit ? FColor::Green : FColor::Red;
    DrawDebugMeleeAttack(DrawColor, Start, End, Forward);
}


void APlayerCharacter::ServerRPCPerformMeleeHit_Implementation(AActor* DamagedActor, float InCheckTime)
{
    if (APlayerBase* DamagedPlayer = Cast<APlayerBase>(DamagedActor))
    {
        float Damage = this->GetAttackDamage();
        UE_LOG(LogTemp, Log, TEXT("%s AttackDamage : %f"), *GetName(), Damage);

        FDamageEvent DamageEvent;
        DamagedPlayer->TakeDamage(Damage, DamageEvent, GetController(), this);

        FString ClientName = TEXT("no Client");
        if (APlayerController* PC = Cast<APlayerController>(DamagedPlayer->GetController()))
        {
            ClientName = PC->GetName();
        }
        UE_LOG(LogTemp, Log, TEXT("%s - HP : %f / %f"), *ClientName, DamagedPlayer->GetHealth(), DamagedPlayer->GetMaxHealth());
    }
}


bool APlayerCharacter::ServerRPCPerformMeleeHit_Validate(AActor* DamagedActor, float InCheckTime)
{
    return true;
}

void APlayerCharacter::CheckItemMeleeAttack()
{
    if (!HasAuthority())
    {
        return;
    }
    if (CurrentEquippedItemName == "DEFAULT" || !ItemManager || !ItemManager->ItemDataTable)
    {
        return;
    }
    const FString Context = TEXT("ItemMeleeAttackContext");
    const FEquipItemDataRow* Row = ItemManager->ItemDataTable->FindRow<FEquipItemDataRow>(CurrentEquippedItemName, Context);
    if (!Row)
    {
        UE_LOG(LogTemp, Error, TEXT("CheckItemMeleeAttack: '%s' 아이템 데이터 없음"), *CurrentEquippedItemName.ToString());
        return;
    }

    const FVector Forward = GetActorForwardVector();
    const FVector Start = GetCapsuleComponent()->GetComponentLocation() + GetActorRightVector() * 50.0f + Forward * GetCapsuleComponent()->GetScaledCapsuleRadius();
    const FVector End = Start + Forward * Row->HitRange;

    TArray<FHitResult> OutHits;
    FCollisionQueryParams Params(NAME_None, false, this);

    bool bHit = GetWorld()->SweepMultiByChannel(
        OutHits,
        Start,
        End,
        FQuat::Identity,
        ECC_Camera,
        FCollisionShape::MakeSphere(Row->HitRadius),
        Params
    );

    const FVector CapsuleCenter = (Start + End) * 0.5f;
    const float CapsuleHalfHeight = Row->HitRange * 0.5f;
    if (CVarDebugGeneral.GetValueOnGameThread()) // [debug] DX.Debug.General 1 일 때만 표시
    DrawDebugCapsule(GetWorld(), CapsuleCenter, CapsuleHalfHeight, Row->HitRadius, FRotationMatrix::MakeFromZ(Forward).ToQuat(), bHit ? FColor::Red : FColor::Blue, false, 2.0f);

    TSet<APlayerBase*> HitPlayers;

    for (const FHitResult& Hit : OutHits)
    {
        APlayerBase* Target = Cast<APlayerBase>(Hit.GetActor());
        if (!Target || Target == this || HitPlayers.Contains(Target))
        {
            continue;
        }
        HitPlayers.Add(Target);

        const float TotalDamage = GetAttackDamage() + Row->AdditionalDamage;
        FDamageEvent DamageEvent;
        Target->TakeDamage(TotalDamage, DamageEvent, GetController(), this);

        FVector KnockForce = Forward * Row->KnockBackForce;
        KnockForce.Z += Row->LaunchUpForce;

        if (UCharacterMovementComponent* MoveComp = Target->GetCharacterMovement())
        {
            MoveComp->Launch(FVector::ZeroVector); // 리셋 후
            MoveComp->Launch(KnockForce);
        }

        if (Row->RagdollTime > 0.f)
        {
            Target->OnStunned(Row->RagdollTime);
        }

        UE_LOG(LogTemp, Log, TEXT("Item Melee Hit: %s (Damage: %.1f Knock: %.1f LaunchZ: %.1f Ragdoll: %.1f)"),
            *Target->GetName(), TotalDamage, Row->KnockBackForce, Row->LaunchUpForce, Row->RagdollTime);
    }
}

void APlayerCharacter::Multicast_CheckItemMeleeAttack_Implementation()
{
    CheckItemMeleeAttack();
}

void APlayerCharacter::Server_CheckItemMeleeAttack_Implementation()
{
    CheckItemMeleeAttack();
}


void APlayerCharacter::DrawDebugMeleeAttack(const FColor& DrawColor, FVector TraceStart, FVector TraceEnd, FVector Forward)
{
    const float MeleeAttackRange = 50.f;
    const float MeleeAttackRadius = 50.f;
    FVector CapsuleOrigin = TraceStart + (TraceEnd - TraceStart) * 0.5f;
    float CapsuleHalfHeight = MeleeAttackRange * 0.5f;
    if (!CVarDebugGeneral.GetValueOnGameThread()) return; // [debug] DX.Debug.General 1 일 때만 표시
    DrawDebugCapsule(GetWorld(), CapsuleOrigin, CapsuleHalfHeight, MeleeAttackRadius, FRotationMatrix::MakeFromZ(Forward).ToQuat(), DrawColor, false, 5.0f);
}

void APlayerCharacter::ResetLeftAttack()
{
    if (HasAuthority())
    {
        PendingAttackOffset = FVector::ZeroVector;
        MulticastResetAttack(bLeftCanAttack);
    }
}

void APlayerCharacter::ResetRightAttack()
{
    if (HasAuthority())
    {
        PendingAttackOffset = FVector::ZeroVector;
        MulticastResetAttack(bRightCanAttack);
    }
}


void APlayerCharacter::ServerRPCLeftHandMeleeAttack_Implementation(float InStartAttackTime)
{
    PendingAttackOffset = GetActorRightVector() * -50.0f;
    PerformMeleeAttack(PendingAttackOffset, LeftMeleeAttackMontage, true);
    MulticastPlayMeleeAttackMontage(LeftMeleeAttackMontage);
}

bool APlayerCharacter::ServerRPCLeftHandMeleeAttack_Validate(float InStartAttackTime)
{
    return true;
}

void APlayerCharacter::ServerRPCRightHandMeleeAttack_Implementation(float InStartAttackTime)
{
    PendingAttackOffset = GetActorRightVector() * 50.0f;
    PerformMeleeAttack(PendingAttackOffset, RightMeleeAttackMontage, false);
    MulticastPlayMeleeAttackMontage(RightMeleeAttackMontage);
}

bool APlayerCharacter::ServerRPCRightHandMeleeAttack_Validate(float InStartAttackTime)
{
    return true;
}

void APlayerCharacter::MulticastPlayMeleeAttackMontage_Implementation(UAnimMontage* AttackMontage)
{
    if (AttackMontage)
    {
        UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
        if (AnimInstance && !AnimInstance->Montage_IsPlaying(AttackMontage))
        {
            AnimInstance->Montage_Play(AttackMontage);
        }
    }
}


void APlayerCharacter::MulticastResetAttack_Implementation(bool bIsLeftHand)
{
    if (bIsLeftHand)
    {
        bLeftCanAttack = true;
    }
    else
    {
        bRightCanAttack = true;
    }

}

void APlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(APlayerCharacter, bLeftCanAttack);
    DOREPLIFETIME(APlayerCharacter, bRightCanAttack);
    DOREPLIFETIME(ThisClass, MatchState);
}

#pragma endregion

void APlayerCharacter::OnStunned()
{
    Super::OnStunned();

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        DisableInput(PC);

        if (ULocalPlayer* LocalPlayer = PC->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
            {
                InputSubsystem->RemoveMappingContext(InputMappingContext);
            }
        }

        PC->SetIgnoreMoveInput(true);
        PC->SetIgnoreLookInput(true);
    }
}

//void APlayerCharacter::RecoverFromStun()
//{
//    Super::RecoverFromStun();
//}

void APlayerCharacter::CharacterMovementOn_Implementation()
{

    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        EnableInput(PC);
        if (ULocalPlayer* LP = PC->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsys = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP))
            {
                Subsys->AddMappingContext(InputMappingContext, 0);
            }
        }
        PC->SetIgnoreMoveInput(false);
        PC->SetIgnoreLookInput(false);
    }
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);

    if (USkeletalMeshComponent* MeshComp = GetMesh())
    {
        MeshComp->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
    }

    if (!HasAuthority())
    {
        Server_ActivateActiveRagdoll();
    }
}

void APlayerCharacter::HandleBKey(const FInputActionValue& Value)
{
    APCController_GamePlay* PlayerController_GamePlay = GetController<APCController_GamePlay>();
    if (IsValid(PlayerController_GamePlay))
    {
        if (PlayerController_GamePlay->IsLocalController() || PlayerController_GamePlay->LobbyWidget)
        {
            PlayerController_GamePlay->OnHandleLobbyUI();
        }
    }
}

void APlayerCharacter::HandleESCKey(const FInputActionValue& Value)
{
    UE_LOG(LogTemp, Log, TEXT("ESCkey"));
}

void APlayerCharacter::SpawnProjectileFromItem(float SpeedFactor, float ForceFactor)
{
    if (!ItemManager)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ItemManagerComponent 값이 null"));
        return;
    }
    if (!ItemManager->ItemDataTable)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ItemDataTable 값이 null"));
        return;
    }

    const FName EquippedItem = CurrentEquippedItemName;
    if (EquippedItem.IsNone() || EquippedItem == FName("DEFAULT"))
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : CurrentEquippedItemName 확인"));
        return;
    }

    static const FString Context = TEXT("ProjectileSpawn");
    const FEquipItemDataRow* Row = ItemManager->ItemDataTable->FindRow<FEquipItemDataRow>(EquippedItem, Context);
    if (!Row)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : '%s' 행을 찾을 수 없음"), *EquippedItem.ToString());
        return;
    }
    if (!Row->ProjectileBlueprint)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : ProjectileBlueprint 값이 null"));
        return;
    }

    if (!MuzzleComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : MuzzleComponent 값이 null"));
        return;
    }

    const FVector SpawnLoc = MuzzleComponent->GetComponentLocation();

    FVector AimDir = GetActorForwardVector();
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (PC->IsLocalController())
        {
            AimDir = PC->GetControlRotation().Vector();
        }
        else if (HasAuthority())
        {
            AimDir = GetActorForwardVector();
        }
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.Instigator = this;

    AActor* Projectile = GetWorld()->SpawnActor<AActor>(
        Row->ProjectileBlueprint,
        SpawnLoc,
        AimDir.Rotation(),
        SpawnParams
    );
    if (!Projectile)
    {
        UE_LOG(LogTemp, Error, TEXT("PlayerCharacter.cpp : Projectile 스폰 실패"));
        return;
    }

    if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(
        Projectile->GetComponentByClass(UPrimitiveComponent::StaticClass())))
    {
        if (Prim->IsSimulatingPhysics())
        {
            const float Speed = Row->ProjectileSpeed * SpeedFactor;
            const float Force = Row->ProjectileForce * ForceFactor;
            const FVector LaunchVelocity = AimDir * Speed;

            Prim->SetPhysicsLinearVelocity(LaunchVelocity, true);
            const float ImpulseScale = (Force / FMath::Max(1.0f, Speed));
            Prim->AddImpulse(LaunchVelocity * ImpulseScale, NAME_None, true);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("PlayerCharacter.cpp : Projectile 물리 시뮬레이션 비활성"));
        }
    }
}


void APlayerCharacter::ServerSpawnProjectileFromItem_Implementation(float SpeedFactor, float ForceFactor)
{
    SpawnProjectileFromItem(SpeedFactor, ForceFactor);
}


void APlayerCharacter::HandleFKey(const FInputActionValue& Value)
{
    if (MatchState != EMatchState::Playing)
    {
        return;
    }

    if (!bRightCanAttack || CurrentEquippedItemName == "DEFAULT" || !ItemManager || !ItemManager->ItemDataTable)
    {
        return;
    }

    const FString Context = TEXT("FKeyContext");
    const FEquipItemDataRow* Row = ItemManager->ItemDataTable->FindRow<FEquipItemDataRow>(CurrentEquippedItemName, Context);
    if (!Row)
    {
        UE_LOG(LogTemp, Error, TEXT("[FKey] Cannot find item row for '%s'"), *CurrentEquippedItemName.ToString());
        return;
    }

    bRightCanAttack = false;

    if (Row->ItemType == EEquipItemType::Melee)
    {
        MulticastPlayMeleeAttackMontage(ItemRightRangedAttackMontage);

        float Duration = ItemRightRangedAttackMontage ? ItemRightRangedAttackMontage->GetPlayLength() : 1.0f;
        GetWorldTimerManager().SetTimer(
            AttackTimerHandle,
            [this]()
            {
                ResetRightAttack();
                MulticastResetAttack(bRightCanAttack);
            },
            Duration,
            false
        );
    }
    else if (Row->ItemType == EEquipItemType::Ranged)
    {
        ServerStartShortThrow();
    }
}

void APlayerCharacter::HandleCheatKey(const FInputActionValue& Value)
{
    // 1번 키 할당
    APCController_GamePlay* PlayerController_GamePlay = GetController<APCController_GamePlay>();
    if (IsValid(PlayerController_GamePlay))
    {
        if (PlayerController_GamePlay->IsLocalController() || PlayerController_GamePlay->LobbyWidget)
        {
            PlayerController_GamePlay->Server_TriggerRandomPlayerWin();
        }
    }
}

void APlayerCharacter::ServerProcessDeath(FVector RespawnLocation)
{
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        PC->UnPossess();
        PC->ChangeState(NAME_Spectating);
    }

    Super::ServerProcessDeath_Implementation(RespawnLocation);
}

void APlayerCharacter::HandleCheat2Key(const FInputActionValue& Value)
{
    // 2번 키 할당
    // 테스트용으로 사용
    // 넣어야 될 내용 공유하고 지우고 커밋하기
    /*
        플레이어 mountainlevel에서 이동할 기능 필요

    */
    APCController_GamePlay* PlayerController_GamePlay = GetController<APCController_GamePlay>();
    if (IsValid(PlayerController_GamePlay))
    {
        if (PlayerController_GamePlay->IsLocalController() || PlayerController_GamePlay->LobbyWidget)
        {
            PlayerController_GamePlay->ServerTeleportToCheckpoint();
        }
    }

    //UE_LOG(LogTemp, Log, TEXT("Cheat2"));
}

void APlayerCharacter::HandleCheat3Key(const FInputActionValue& Value)
{
    // 키보드 3번 할당
    UE_LOG(LogTemp, Warning, TEXT("3번키"));
}

void APlayerCharacter::HandleCheat4Key(const FInputActionValue& Value)
{
    // 키보드 4번 할당
    UE_LOG(LogTemp, Warning, TEXT("4번키"));
}

void APlayerCharacter::HandleCheat5Key(const FInputActionValue& Value)
{
    // 키보드 5번 할당
    UE_LOG(LogTemp, Warning, TEXT("5번키"));
    MatchState = EMatchState::End;
    PlayResultMontage();
}

void APlayerCharacter::HandleGrabKey(const FInputActionValue& Value)
{
    SetSkinName(TEXT("Deer"));
}

void APlayerCharacter::ServerStartGrab_Implementation()
{
    if (GrabActorInstance)
    {
        GrabActorInstance->ActivateGrab();
        MulticastActivateGrab();
    }
}


void APlayerCharacter::ServerStopGrab_Implementation()
{
    if (GrabActorInstance)
    {
        GrabActorInstance->ReleaseGrab();
        MulticastReleaseGrab();
    }
}


void APlayerCharacter::MulticastActivateGrab_Implementation()
{
    if (!HasAuthority() && GrabActorInstance)
    {
        GrabActorInstance->ActivateGrab();
    }
}

void APlayerCharacter::MulticastReleaseGrab_Implementation()
{
    if (!HasAuthority() && GrabActorInstance)
    {
        GrabActorInstance->ReleaseGrab();
    }
}

void APlayerCharacter::GrabTarget(USkeletalMeshComponent* TargetMesh, const FName& TargetBone)
{
    if (!HandConstraint || !TargetMesh) return;

    HandConstraint->SetConstrainedComponents(GetMesh(), TEXT("hand_l"),
        TargetMesh, TargetBone);

    HandConstraint->ConstraintInstance.SetLinearXLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
    HandConstraint->ConstraintInstance.SetLinearYLimit(ELinearConstraintMotion::LCM_Locked, 0.f);
    HandConstraint->ConstraintInstance.SetLinearZLimit(ELinearConstraintMotion::LCM_Locked, 0.f);

    HandConstraint->ConstraintInstance.SetAngularSwing1Limit(EAngularConstraintMotion::ACM_Limited, 45.f);
    HandConstraint->ConstraintInstance.SetAngularSwing2Limit(EAngularConstraintMotion::ACM_Limited, 45.f);
    HandConstraint->ConstraintInstance.SetAngularTwistLimit(EAngularConstraintMotion::ACM_Limited, 45.f);

    UE_LOG(LogTemp, Warning, TEXT("GrabTarget: 본 [%s]와 연결"), *TargetBone.ToString());
}


void APlayerCharacter::PlayResultMontage()
{
    if (MatchState == EMatchState::End && ResultMontage)
    {
        if (UAnimInstance* Anim = GetMesh()->GetAnimInstance())
        {
            if (!Anim->Montage_IsPlaying(ResultMontage))
            {
                Anim->Montage_Play(ResultMontage, 1.f, EMontagePlayReturnType::MontageLength, 0.f, true);
            }
        }
    }
}

void APlayerCharacter::Server_RemoveEquippedItem_Implementation()
{
    ServerSetEquippedItemName("DEFAULT");
}
