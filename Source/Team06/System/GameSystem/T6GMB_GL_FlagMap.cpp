// Fill out your copyright notice in the Description page of Project Settings.


#include "System/GameSystem/T6GMB_GL_FlagMap.h"
#include "System/GameSystem/T6GSB_GL_FlagMap.h"
#include "Engine/PlayerStartPIE.h"
#include "Player/Controller/PCController_GamePlay.h"
void AT6GMB_GL_FlagMap::BeginPlay()
{
	Super::BeginPlay();

	RemainFlagModePlayingTime = FlagModeLevelPlayingTime;



	AT6GSB_GL_FlagMap* GSB_F = GetGameState<AT6GSB_GL_FlagMap>();

	if (GSB_F)
	{
		GSB_F->InitSpawnPoint();
		SpreadPlayerControllers();
		GSB_F->SpawnFlagActor();
	}

//	GetWorld()->GetTimerManager().SetTimer(FlagMapPlayingTimer, this, &ThisClass::OnMainTimerElapsed, 1.f, true);
}

void AT6GMB_GL_FlagMap::SpreadPlayerControllers()
{
	AT6GSB_GL_FlagMap* GSB_F = GetGameState<AT6GSB_GL_FlagMap>();
	if (GSB_F)
	{
		int32 SpawnPointAmount = GSB_F->SpawnPoints.Num();
		for (APCController_GamePlay* PCC : SessionPlayerControllers)
		{
			if (SpawnPointAmount == 0)
			{
				UE_LOG(LogTemp, Error, TEXT("SpawnPoints가 없습니다!"));
				return;
			}

			RestartPlayerAtPlayerStart(PCC, GSB_F->SpawnPoints[SpawnIdx % SpawnPointAmount]);
			SpawnIdx++;
			
		}
	}

}

void AT6GMB_GL_FlagMap::OnFlagTimerEnd()
{
	AT6GSB_GL_FlagMap* GSB_F = GetGameState<AT6GSB_GL_FlagMap>();
	if (GSB_F)
	{
		AController* Winner = GSB_F->GetFlagOwningController();
		if (APlayerController* PC = Cast<APlayerController>(Winner))
		{
			HandlePlayerGameWin(PC);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid PlayerController at OnFlagTimerEnd"));
		}
	}
}

void AT6GMB_GL_FlagMap::OnMainTimerElapsed()
{
	AT6GameStateBase_GameLevel* MPGameState = GetGameState<AT6GameStateBase_GameLevel>();
	if (IsValid(MPGameState) == false)
	{
		return;
	}

	switch (MPGameState->MatchState)
	{
	case EMatchState::None:
		break;
	case EMatchState::Waiting:
	{
		FString NotificationString = FString::Printf(TEXT(""));

		if (SessionPlayerControllers.Num() < MinimumPlayerCountForPlaying)
		{
			NotificationString = FString::Printf(TEXT("Wait another players for playing."));
			RemainGameLevelWaitingTimeForPlaying = GameLevelWaitingTime; // 최소인원이 안된다면 대기 시간 초기화.
		}
		else
		{
			if (!IsMajorityReady())
			{ //Ready한 사람 부족
				NotificationString = FString::Printf(TEXT("Need morePlayer To Ready"));

				RemainGameLevelWaitingTimeForPlaying = GameLevelWaitingTime; // 최소인원이 안된다면 대기 시간 초기화.
			}
			else
			{
				NotificationString = FString::Printf(TEXT("Wait %d seconds for playing."), RemainGameLevelWaitingTimeForPlaying);
				//UE_LOG(LogTemp, Warning, TEXT("CountDown -1 sec"));
				--RemainGameLevelWaitingTimeForPlaying;
			}
		}

		if (RemainGameLevelWaitingTimeForPlaying <= 0)
		{
			NotificationString = FString::Printf(TEXT("Game Start!!!!"));
			MPGameState->MatchState = EMatchState::Playing;
		}

		NotifyToAllPlayer(NotificationString);

		break;
	}
	case EMatchState::Playing:
	{
		FString NotificationString = FString::Printf(TEXT(""));
		RemainGameLevelWaitingTimeForPlaying = GameLevelWaitingTime; // 기다리는 시간 초기화

		RemainFlagModePlayingTime--;
		NotificationString = FString::Printf(TEXT("Remain Time: %d"),RemainFlagModePlayingTime);
		NotifyToAllPlayer(NotificationString);

		if (RemainFlagModePlayingTime <= 0)
		{
			OnFlagTimerEnd();
		}

		break;
	}

	case EMatchState::Ending:
	{
		FString NotificationString = FString::Printf(TEXT("Game Finished!!! Next Game Will Starts in %d"), RemainGameStartTimeForEnding);

		NotifyToAllPlayer(NotificationString);

		--RemainGameStartTimeForEnding;

		if (RemainGameStartTimeForEnding <= 0)
		{
			GetWorldTimerManager().ClearTimer(MainTimerHandle); // [5.8 migration] Invalidate()는 핸들만 비우고 타이머는 계속 실행됨 → 이중 타이머로 카운트다운 2배속
			MPGameState->MatchState = EMatchState::Loading;
			OnShowingLoadingScreen();
			ChangeGameLevel();
			return;
		}

		break;
	}
	case EMatchState::Loading:
	{
		break;
	}
	case EMatchState::End:
	{
		FString NotificationString = FString::Printf(TEXT("Loading GameResult in %d"), RemainGameStartTimeForEnding);

		NotifyToAllPlayer(NotificationString);

		--RemainGameStartTimeForEnding;

		if (RemainGameStartTimeForEnding <= 0)
		{
			// Gameover
			GetWorldTimerManager().ClearTimer(MainTimerHandle); // [5.8 migration] Invalidate()는 핸들만 비우고 타이머는 계속 실행됨 → 이중 타이머로 카운트다운 2배속
			MPGameState->MatchState = EMatchState::Loading;
			OnShowingLoadingScreen();
			GetWorld()->ServerTravel(GameOvermap, true);
			return;
		}


		break;
	}

	default:
		break;
	}
}
