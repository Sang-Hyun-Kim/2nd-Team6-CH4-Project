// Fill out your copyright notice in the Description page of Project Settings.


#include "System/GameSystem/T6GameModeBase_GameResult.h"
#include "System/GameSystem/T6GameInstance.h"
#include "Player/PlayerState/PlayerCharacterState.h"
#include "EngineUtils.h"
#include "Engine/PlayerStartPIE.h"
#include "Player/Controller/PCController_GamePlay.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameSession.h"
#include "System/GameSystem/T6GameStateBase_GameResult.h"
#include "System/GameSystem/FlagSpawnBox.h"

void AT6GameModeBase_GameResult::BeginPlay()
{
    Super::BeginPlay();

    if (HasAuthority())
    {
        if (UT6GameInstance* GI = GetGameInstance<UT6GameInstance>())
        {
            UE_LOG(LogTemp, Warning, TEXT("Final winner is: %s"), *GI->FinalWinnerController);
            Winner = GI->FinalWinnerController;
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Can't Read GameInstnace at GR_BeginPlay"));
        }
    }
    GetWorld()->GetTimerManager().SetTimer(MainTimerHandle, this, &ThisClass::OnMainTimerElapsed, 1.f, true);


}

void AT6GameModeBase_GameResult::StartPlay()
{
    Super::StartPlay();
    AT6GameStateBase_GameResult* GSB = GetGameState<AT6GameStateBase_GameResult>();
    if (!GSB)
    {
        UE_LOG(LogTemp, Error, TEXT("GameStateBase가 유효하지 않습니다."));
        return;
    }
    else
    {
        GSB->InitGameResultSpawnPoint();
    }
}

void AT6GameModeBase_GameResult::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
    FString NewPlayerName = NewPlayer->PlayerState->GetPlayerName();
    AT6GameStateBase_GameResult* GSB = GetGameState<AT6GameStateBase_GameResult>();

    if (GSB && GSB->WinnerSpawnPoint && NewPlayerName == Winner)
    {
        FTransform WinnerTransform = GSB->WinnerSpawnPoint->GetActorTransform();
        APawn* NewPawn = SpawnDefaultPawnAtTransform(NewPlayer, WinnerTransform);
        if (NewPawn)
        {
            NewPlayer->Possess(NewPawn);
            return; // 기본 처리 막기
        }
    }

    // 기본 처리로 넘어감
    Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AT6GameModeBase_GameResult::OnGameResultTimerFinished()
{
    UE_LOG(LogTemp, Warning, TEXT("GameResult: Time expired. Resetting the game and moving to Lobby."));
    ResetAndReturnToLobby();
}

void AT6GameModeBase_GameResult::ResetAndReturnToLobby()
{
    for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
    {
        APlayerController* PC = Iterator->Get();
        if (PC && PC->GetNetConnection())
        {
            UE_LOG(LogTemp, Log, TEXT("Kicking player %s"), *PC->GetPlayerState<APlayerState>()->GetPlayerName());
            GameSession->KickPlayer(PC, FText::FromString(TEXT("Game has Ended")));
        }
    }

    // GameInstance 초기화
    if (UT6GameInstance* GI = GetGameInstance<UT6GameInstance>())
    {
        GI->PlayerScoreBoard.Empty();
        GI->FinalWinnerController = TEXT("NoOne");
    }

    // Lobby 레벨로 이동
    const FString LobbyMapPath = TEXT("/Game/Team6/GameSystem/GS_Level/TestLobby?listen");
    UGameplayStatics::OpenLevel(this, FName(*LobbyMapPath), true);
}

void AT6GameModeBase_GameResult::OnMainTimerElapsed()
{
    --RemainGameResultClosingTime;
    FString NotificationString = FString::Printf(TEXT(""));
    NotificationString = FString::Printf(TEXT("Wait %d seconds for ServerClosing."), RemainGameResultClosingTime);
    NotifyToAllPlayer(NotificationString);

    if (RemainGameResultClosingTime <=0)
    {
        GetWorldTimerManager().ClearTimer(MainTimerHandle); // [5.8 migration] Invalidate()는 핸들만 비우고 타이머는 계속 실행됨 → 이중 타이머로 카운트다운 2배속
        
        OnGameResultTimerFinished();
    }
}
