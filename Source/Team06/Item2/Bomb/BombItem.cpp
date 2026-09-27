#include "Item2/Bomb/BombItem.h"
#include "Team06.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "DrawDebugHelpers.h"
#include "Player/Player/PlayerBase.h"

ABombItem::ABombItem()
{
    bReplicates = true;
    SetReplicateMovement(false);

    if (MeshComp)
    {
        MeshComp->SetSimulatePhysics(false);
    }
}

void ABombItem::OnSpawn()
{
    Super::OnSpawn();
    if (HasAuthority() && ExplosionDelay > 0.f)
    {
        GetWorld()->GetTimerManager().SetTimer(
            ExplosionTimerHandle,
            this,
            &ABombItem::Explode,
            ExplosionDelay,
            false
        );
    }
}

void ABombItem::OnCollision(AActor* OtherActor)
{
    if (!HasAuthority() || !OtherActor) return;
    if (APlayerBase* Player = Cast<APlayerBase>(OtherActor))
    {
        Player->OnStunned();
        ApplyEffect(Player);
    }

    GetWorld()->GetTimerManager().ClearTimer(ExplosionTimerHandle);
    Explode();
}

void ABombItem::Explode()
{
    if (!HasAuthority()) return;

    ActivateParticle1();
    ServerPlaySound1();

    // [fix] 기존에는 NetMulticast 안에서 실행되어 클라이언트에서도 데미지/넉백이 계산됨 → 서버 권한으로 이동
    UGameplayStatics::ApplyRadialDamage(
        GetWorld(),
        Damage,
        GetActorLocation(),
        ExplosionRadius,
        UDamageType::StaticClass(),
        TArray<AActor*>(),
        this,
        GetInstigatorController(),
        true
    );

    TArray<AActor*> Overlapping;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerBase::StaticClass(), Overlapping);
    for (AActor* A : Overlapping)
    {
        APlayerBase* P = Cast<APlayerBase>(A);
        if (!P) continue;
        float Dist = FVector::Dist(GetActorLocation(), P->GetActorLocation());
        if (Dist > ExplosionRadius) continue;
        FVector Dir = (P->GetActorLocation() - GetActorLocation()).GetSafeNormal();
        float Strength = KnockBackMultiplier * 1000.f * (1.f - Dist / ExplosionRadius);
        FVector Vel = Dir * Strength + FVector(0, 0, Strength * 0.5f);
        P->LaunchCharacter(Vel, true, true);
    }

    MulticastExplode();
    Destroy();
}

void ABombItem::MulticastExplode_Implementation()
{
    // [fix] 연출 전용. 데미지/넉백은 서버(Explode)에서만 처리한다.
    if (CVarDebugGeneral.GetValueOnGameThread())
    {
        DrawDebugSphere(GetWorld(), GetActorLocation(), ExplosionRadius, 32, FColor::Red, false, 3.0f);
    }
}
