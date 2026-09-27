// Fill out your copyright notice in the Description page of Project Settings.


#include "System/GameSystem/T6GameModeBase_GameLevel.h"
#include "System/GameSystem/T6GameInstance.h"
#include "System/GameSystem/T6GameStateBase.h"
#include "Player/Controller/PCController_GamePlay.h"
#include "player/PlayerState/PlayerCharacterState.h"
#include "System/GameSystem/T6GameStateBase_GameLevel.h"

void AT6GameModeBase_GameLevel::HandlePlayerGameWin(AController* Winner)
{
    if (!Winner || !Winner->PlayerState) return;

    FString Name = Winner->PlayerState->GetPlayerName();

	if (!bIsGameLevelFinished)
	{
		if (UT6GameInstance* GI = GetGameInstance<UT6GameInstance>())
		{
			GI->AddWinForPlayer(Name);
			UE_LOG(LogTemp, Log, TEXT("%s now has +1 win"), *Name);
			GetWorldTimerManager().ClearTimer(MainTimerHandle); // [5.8 migration] Invalidate()는 핸들만 비우고 타이머는 계속 실행됨 → 이중 타이머로 카운트다운 2배속
			if (OnGameWinHandled.IsBound())
			{
				UE_LOG(LogTemp, Warning, TEXT("MainTimerHandle invalidated"));
				OnGameWinHandled.Execute(Winner);
			}
		}
		
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Game AlreayFinished"));
	}

}

void AT6GameModeBase_GameLevel::HandleRandomPlayerGameWin()
{
	if (!bIsGameLevelFinished)
	{
		int32 PlayerCount = SessionPlayerControllers.Num();
		if (PlayerCount <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("No Player in This GameMode, No one can win!!!"));
		}
		else
		{
			int32 RandIdx = FMath::RandRange(0, PlayerCount - 1);
			APCController_GamePlay* RandomPC = SessionPlayerControllers[RandIdx];

			if (RandomPC)
			{
				FString RandomPlayerName = RandomPC->GetPlayerState<APlayerCharacterState>()->GetPlayerName();
				if (!RandomPlayerName.IsEmpty())
				{
					UT6GameInstance* GI = GetGameInstance<UT6GameInstance>();
					if (IsValid(GI))
					{
						GI->AddWinForPlayer(RandomPlayerName);
						UE_LOG(LogTemp, Log, TEXT("%s now has +1 win"), *RandomPlayerName);
						GetWorldTimerManager().ClearTimer(MainTimerHandle); // [5.8 migration] Invalidate()는 핸들만 비우고 타이머는 계속 실행됨 → 이중 타이머로 카운트다운 2배속
						if (OnGameWinHandled.IsBound())
						{
							UE_LOG(LogTemp, Warning, TEXT("MainTimerHandle invalidated"));
							OnGameWinHandled.Execute(RandomPC);
						}
					}
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Invalid PlayerController in This GameMode, No one can win!!!"));
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("GameLevel donesn't finish yet!!!"));
	}
}

void AT6GameModeBase_GameLevel::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);
    AT6GameStateBase_GameLevel* T6GameState = GetGameState<AT6GameStateBase_GameLevel>();
    if (IsValid(T6GameState) == false)
    {
        UE_LOG(LogTemp, Warning, TEXT("GameStatebase get by AT6GameStateBase_GameLevel is not AT6GameStateBase_GameLevel"));
        return;
    }
    APCController_GamePlay* NewPlayerController = Cast<APCController_GamePlay>(NewPlayer);
    if (IsValid(NewPlayerController) == true)
    {
        GameLevelPlayerReady.Add(NewPlayerController, 0); // 로미 Ready 관리
    }
}

void AT6GameModeBase_GameLevel::Logout(AController* Exiting)
{
    APCController_GamePlay* ExitingPlayerController = Cast<APCController_GamePlay>(Exiting);
    if (IsValid(ExitingPlayerController) == true && SessionPlayerControllers.Find(ExitingPlayerController) != INDEX_NONE)
    {
        GameLevelPlayerReady.Remove(ExitingPlayerController); // 레디 상태도 제거
    }
    // 로비 게임모드에서 로비 인원들을 지우고,게임모드베이스에서 세션 컨트롤러 삭제하는 순서
    Super::Logout(Exiting);
}

void AT6GameModeBase_GameLevel::BeginPlay()
{
    Super::BeginPlay();
	OnGameWinHandled.BindUObject(this, &ThisClass::InitiatedGameLevelEnding);
    GetWorld()->GetTimerManager().SetTimer(MainTimerHandle, this, &ThisClass::OnMainTimerElapsed, 1.f, true);
    RemainGameStartTimeForEnding = GameStartCountDown;

	UT6GameInstance* GI = GetGameInstance<UT6GameInstance>();
	if (GI&&HasAuthority())
	{
		GI->PrintScoreBoardLog();
	}
}

void AT6GameModeBase_GameLevel::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	APCController_GamePlay* PCC_GP = Cast<APCController_GamePlay>(NewPlayer);
	if (IsValid(PCC_GP))
	{
		UT6GameInstance* GI = GetGameInstance<UT6GameInstance>();

		PCC_GP->Client_StartScoreBoard(GI->PackingPlayerScore());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Can't Access Score in HandleStartingNewPlayer"));
	}

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}

void AT6GameModeBase_GameLevel::OnMainTimerElapsed()
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
		// RemainGameLevelWaitingTimeForPlaying = GameLevelWaitingTime;


		// 최소인원 넘기기
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
		// 게임중일 때 GameStateBase_GameLevel가 해야할일
		// MPGameState->AlivePlayerContrReadyCountollerCount = SessionPlayerControllers.Num();

		RemainGameLevelWaitingTimeForPlaying = GameLevelWaitingTime; // 기다리는 시간 초기화
		
		
		// 누가 이겼다면
		if(bIsGameLevelFinished)
		{
			if (!bIsGameFinallyFinished)
			{
				MPGameState->MatchState = EMatchState::Ending;
			}
			else
			{
				MPGameState->MatchState = EMatchState::End;
			}
			
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

void AT6GameModeBase_GameLevel::SetPlayerReady(APCController_GamePlay* PlayerController, bool bIsReady)
{
	if (IsValid(PlayerController) == false)
		return;

	GameLevelPlayerReady.FindOrAdd(PlayerController) = bIsReady;

	int32 ReadyCount = 0;
	for (auto& Elem : GameLevelPlayerReady)
	{
		if (Elem.Value)
		{
			ReadyCount++;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("SetPlayerReady %d Ready"), ReadyCount)
		FString Noti = FString::Printf(TEXT("Ready: %d / %d"), ReadyCount, SessionPlayerControllers.Num());
	NotifyToAllPlayer(Noti);

	// 준비 상태 확인 후 MatchState 변경
	AT6GameStateBase_GameLevel* MPGameState = GetGameState<AT6GameStateBase_GameLevel>();
	if (!IsValid(MPGameState)) return;

	// MPGameState->ReadyPlayerCount = ReadyCount;

	if (IsMajorityReady())
	{
		MPGameState->MatchState = EMatchState::Waiting;
	}
}

bool AT6GameModeBase_GameLevel::IsMajorityReady() const
{
	int32 ReadyCount = 0;

	for (auto& Elem : GameLevelPlayerReady)
	{
		if (Elem.Value)
		{
			ReadyCount++;
		}
	}

	return ReadyCount > (SessionPlayerControllers.Num() / 2);
}

void AT6GameModeBase_GameLevel::InitiatedGameLevelEnding(AController* Winner)
{
	AT6GameStateBase_GameLevel* MPGameState = GetGameState<AT6GameStateBase_GameLevel>();
	if (IsValid(MPGameState) == false)
	{
		return;
	}
	bIsGameLevelFinished = true;

	if (!bIsGameFinallyFinished)
	{
		MPGameState->MatchState = EMatchState::Ending;
	}
	else
	{
		MPGameState->MatchState = EMatchState::End;
	}



	if (Winner)
	{
		FString WinnerName = Winner->GetPlayerState<APlayerCharacterState>()->GetPlayerName();
		// 승리 관련 하이라이트 하기
		FString NotificationString = FString::Printf(TEXT("GameWinner is %s"), *WinnerName);

		NotifyToAllPlayer(NotificationString);

	}
	UpdateAllClientScoreBoards();
	GetWorld()->GetTimerManager().SetTimer(MainTimerHandle, this, &ThisClass::OnMainTimerElapsed, 1.f, true);
}

void AT6GameModeBase_GameLevel::UpdateAllClientScoreBoards()
{
	for (AController* Player : SessionPlayerControllers)
	{
		APCController_GamePlay* GPPlayer = Cast<APCController_GamePlay>(Player);
		if (IsValid(GPPlayer))
		{
			if (UT6GameInstance* GI = GetGameInstance<UT6GameInstance>())
			{
				GPPlayer->Client_UpdateScoreBoard(GI->PackingPlayerScore());
			}
		}
	}
}
