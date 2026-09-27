// Fill out your copyright notice in the Description page of Project Settings.


#include "AI/Character/AICharacter.h"
#include "Team06.h"
#include "AI/System/AIC_Enemy.h"
#include "Components/SphereComponent.h"
#include "Player/Component/FlagActor.h"
#include "BrainComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/DamageType.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/GameStateBase.h"
#include "Player/Component/ItemManagerComponent.h"
#include "Player/Component/EquipItemMeshActor.h" 
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "DrawDebugHelpers.h"

AAICharacter::AAICharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	AIControllerClass = AAIC_Enemy::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	DetectorRadius = 50.f;

	DetectingFlagCollision = CreateDefaultSubobject<USphereComponent>(TEXT("FlagDetector"));
	DetectingFlagCollision->InitSphereRadius(DetectorRadius);
	DetectingFlagCollision->SetupAttachment(GetRootComponent());

	DetectingFlagCollision->OnComponentBeginOverlap.AddDynamic(this, &AAICharacter::OnOverlapBegin);
}

//void AAICharacter::Tick(float DeltaTime)
//{
//	Super::Tick(DeltaTime);
//
//	UE_LOG(LogTemp, Warning, TEXT("Item : %s"), *GetCurrentEquippedItemName().ToString());
//}

void AAICharacter::OnStunned()
{
	Super::OnStunned();
}

void AAICharacter::OnStunned(float StunTime)
{
	Super::OnStunned(StunTime);

	AAIC_Enemy* AIC = Cast<AAIC_Enemy>(GetController());
	if (IsValid(AIC))
	{
		AIC->GetBrainComponent()->StopLogic(TEXT("Stunned"));
	}
}

void AAICharacter::RecoverFromStun()
{
	Super::RecoverFromStun();

	FTimerHandle RecoveryMovementHandle;
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(RecoveryMovementHandle, this, &AAICharacter::AIMovementOn, 2.f, false);
	}
}

void AAICharacter::AIMovementOn()
{
	AAIC_Enemy* AIC = Cast<AAIC_Enemy>(GetController());
	if (IsValid(AIC))
	{
		AIC->GetBrainComponent()->StartLogic();
	}
}

void AAICharacter::OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepHitResult)
{
	if(!MyFlag)
	{
		AFlagActor* Flag = Cast<AFlagActor>(OtherActor);
		if (IsValid(Flag))
		{
			AAIC_Enemy* AIC = Cast<AAIC_Enemy>(GetController());
			if (AIC)
			{
				Flag->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, AttachSocketName);
				Flag->SetOwner(this);

				MyFlag = Flag;
				SetbHasFlag(true);

				AIC->SetChaseMode(false);
			}
		}
	}
	else
	{
		APlayerBase* OtherPlayer = Cast<APlayerBase>(OtherActor);
		if (IsValid(OtherPlayer))
		{
			if (IsValid(OtherPlayer->GetMesh()))
			{
				AAIC_Enemy* AIC = Cast<AAIC_Enemy>(GetController());
				if (AIC)
				{
					MyFlag->AttachToComponent(OtherPlayer->GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, AttachSocketName);
					SetOwner(OtherPlayer);

					MyFlag = nullptr;
					SetbHasFlag(false);

					AIC->SetChaseMode(true);
				}
			}
		}
	}
}

#pragma region Attack
void AAICharacter::HandleLeftHandMeleeAttack()
{
	if (!bLeftCanAttack || GetbIsStunned())
	{
		return;
	}

	float StartAttackTime = GetWorld()->GetTimeSeconds();
	bLeftCanAttack = false;
	ServerRPCLeftHandMeleeAttack(StartAttackTime);
}

void AAICharacter::HandleRightHandMeleeAttack()
{
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

void AAICharacter::ServerRPCItemMeleeAttack_Implementation(float InStartTime)
{
	PendingAttackOffset = GetActorRightVector() * 50.0f;
	PerformMeleeAttack(PendingAttackOffset, ItemRightMeleeAttackMontage, false);
	MulticastPlayMeleeAttackMontage(ItemRightMeleeAttackMontage);
	ServerSetEquippedItemName("DEFAULT");
}

bool AAICharacter::ServerRPCItemMeleeAttack_Validate(float InStartTime)
{
	return true;
}

void AAICharacter::ServerRPCItemRangedAttack_Implementation(float InStartTime)
{
	bRightCanAttack = false;
	PerformRangedAttack(ItemRightRangedAttackMontage);
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

bool AAICharacter::ServerRPCItemRangedAttack_Validate(float InStartTime)
{
	return true;
}


void AAICharacter::PerformMeleeAttack(const FVector& Offset, UAnimMontage* Montage, bool bIsLeftHand)
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

void AAICharacter::PerformRangedAttack(UAnimMontage* AttackMontage)
{
	UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance();
	if (AnimInstance && AttackMontage)
	{
		AnimInstance->Montage_Play(AttackMontage);

		float MontageDuration = AttackMontage->GetPlayLength();
		GetWorld()->GetTimerManager().SetTimer(AttackTimerHandle, this, &AAICharacter::ResetRightAttack, MontageDuration, false);
	}
	else
	{
		ResetRightAttack();
	}
}


void AAICharacter::CheckMeleeAttackHit(const FVector& AttackOffset)
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


void AAICharacter::ServerRPCPerformMeleeHit_Implementation(AActor* DamagedActor, float InCheckTime)
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


bool AAICharacter::ServerRPCPerformMeleeHit_Validate(AActor* DamagedActor, float InCheckTime)
{
	return true;
}

void AAICharacter::DrawDebugMeleeAttack(const FColor& DrawColor, FVector TraceStart, FVector TraceEnd, FVector Forward)
{
	const float MeleeAttackRange = 50.f;
	const float MeleeAttackRadius = 50.f;
	FVector CapsuleOrigin = TraceStart + (TraceEnd - TraceStart) * 0.5f;
	float CapsuleHalfHeight = MeleeAttackRange * 0.5f;
	if (!CVarDebugGeneral.GetValueOnGameThread()) return; // [debug]
	DrawDebugCapsule(GetWorld(), CapsuleOrigin, CapsuleHalfHeight, MeleeAttackRadius, FRotationMatrix::MakeFromZ(Forward).ToQuat(), DrawColor, false, 5.0f);
}

void AAICharacter::ResetLeftAttack()
{
	if (HasAuthority())
	{
		PendingAttackOffset = FVector::ZeroVector;
		MulticastResetAttack(bLeftCanAttack);
	}
}

void AAICharacter::ResetRightAttack()
{
	if (HasAuthority())
	{
		PendingAttackOffset = FVector::ZeroVector;
		MulticastResetAttack(bRightCanAttack);
	}
}


void AAICharacter::ServerRPCLeftHandMeleeAttack_Implementation(float InStartAttackTime)
{
	PendingAttackOffset = GetActorRightVector() * -50.0f;
	PerformMeleeAttack(PendingAttackOffset, LeftMeleeAttackMontage, true);
	MulticastPlayMeleeAttackMontage(LeftMeleeAttackMontage);
}

bool AAICharacter::ServerRPCLeftHandMeleeAttack_Validate(float InStartAttackTime)
{
	return true;
}

void AAICharacter::ServerRPCRightHandMeleeAttack_Implementation(float InStartAttackTime)
{
	PendingAttackOffset = GetActorRightVector() * 50.0f;
	PerformMeleeAttack(PendingAttackOffset, RightMeleeAttackMontage, false);
	MulticastPlayMeleeAttackMontage(RightMeleeAttackMontage);
}

bool AAICharacter::ServerRPCRightHandMeleeAttack_Validate(float InStartAttackTime)
{
	return true;
}

void AAICharacter::MulticastPlayMeleeAttackMontage_Implementation(UAnimMontage* AttackMontage)
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

void AAICharacter::MulticastResetAttack_Implementation(bool bIsLeftHand)
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

void AAICharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAICharacter, bLeftCanAttack);
	DOREPLIFETIME(AAICharacter, bRightCanAttack);
}

void AAICharacter::SpawnProjectileFromItem()
{
	if (!ItemManager)
	{
		return;
	}
	if (!ItemManager->ItemDataTable)
	{
		return;
	}
	const FName EquippedItem = CurrentEquippedItemName;
	if (EquippedItem.IsNone() || EquippedItem == FName("DEFAULT"))
	{
		return;
	}
	static const FString Context = TEXT("ProjectileSpawn");
	const FEquipItemDataRow* Row = ItemManager->ItemDataTable->FindRow<FEquipItemDataRow>(EquippedItem, Context);
	if (!Row)
	{
		return;
	}
	if (!Row->ProjectileBlueprint)
	{
		return;
	}
	if (!MuzzleComponent)
	{
		return;
	}
	const FTransform MuzzleTransform = MuzzleComponent->GetComponentTransform();
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;
	AActor* Projectile = GetWorld()->SpawnActor<AActor>(
		Row->ProjectileBlueprint,
		MuzzleTransform.GetLocation(),
		MuzzleTransform.GetRotation().Rotator(),
		SpawnParams
	);
	if (!Projectile)
	{
		return;
	}
	UPrimitiveComponent* PrimitiveComp = Cast<UPrimitiveComponent>(
		Projectile->GetComponentByClass(UPrimitiveComponent::StaticClass())
	);
	if (!PrimitiveComp)
	{
		return;
	}
	if (!PrimitiveComp->IsSimulatingPhysics())
	{
		return;
	}
	const FVector ForwardVector = MuzzleTransform.GetRotation().GetForwardVector();
	const FVector LaunchVelocity = ForwardVector * Row->ProjectileSpeed;
	const FVector LaunchForce = ForwardVector * Row->ProjectileForce;
	PrimitiveComp->SetPhysicsLinearVelocity(LaunchVelocity, true);
	PrimitiveComp->AddImpulse(LaunchForce, NAME_None, true);
}

#pragma endregion

void AAICharacter::ServerProcessDeath(FVector RespawnLocation)
{
	Super::ServerProcessDeath_Implementation(RespawnLocation);
}
