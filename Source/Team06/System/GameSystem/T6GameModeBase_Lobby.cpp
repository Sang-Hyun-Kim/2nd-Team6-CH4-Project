// Fill out your copyright notice in the Description page of Project Settings.


#include "System/GameSystem/T6GameModeBase_Lobby.h"
#include "Player/Controller/PCController_GamePlay.h"
#include "System/GameSystem/T6GameStateBase_Lobby.h"

void AT6GameModeBase_Lobby::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	AT6GameStateBase_Lobby* T6GameState = GetGameState<AT6GameStateBase_Lobby>();
	if (IsValid(T6GameState) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameStatebase get by GameModebase_Lobby is not GameState_Lobby"));
		return;
	}
	APCController_GamePlay* NewPlayerController = Cast<APCController_GamePlay>(NewPlayer);
	if (IsValid(NewPlayerController) == true)
	{
		LobbyPlayerReady.Add(NewPlayerController, 0); // 로미 Ready 관리
	}
}

void AT6GameModeBase_Lobby::Logout(AController* Exiting)
{

	APCController_GamePlay* ExitingPlayerController = Cast<APCController_GamePlay>(Exiting);
	if (IsValid(ExitingPlayerController) == true && SessionPlayerControllers.Find(ExitingPlayerController) != INDEX_NONE)
	{
		LobbyPlayerReady.Remove(ExitingPlayerController); // 레디 상태도 제거
	}
	// 로비 게임모드에서 로비 인원들을 지우고,게임모드베이스에서 세션 컨트롤러 삭제하는 순서
	Super::Logout(Exiting);

}

void AT6GameModeBase_Lobby::BeginPlay()
{
	Super::BeginPlay();
	AT6GameStateBase * GS = GetGameState<AT6GameStateBase>();
	if (GS)
	{
		GS->bIsLobbyMode = true;
	}
	GetWorld()->GetTimerManager().SetTimer(MainTimerHandle, this, &ThisClass::OnMainTimerElapsed, 1.f, true);
	RemainWaitingTimeForEnding = LobbyEndingTime;
}

void AT6GameModeBase_Lobby::OnMainTimerElapsed()
{
	AT6GameStateBase_Lobby* MPGameState = GetGameState<AT6GameStateBase_Lobby>();
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
		RemainWaitingTimeForEnding = LobbyEndingTime;


		// 최소인원 넘기기
		if (SessionPlayerControllers.Num() < MinimumPlayerCountForPlaying)
		{
			NotificationString = FString::Printf(TEXT("Wait another players for playing."));
			RemainLobbyWaitingTimeForPlaying = LobbyWaitingTime; // 최소인원이 안된다면 대기 시간 초기화.
		}
		else
		{
			if (!IsMajorityReady())
			{ //Ready한 사람 부족
				NotificationString = FString::Printf(TEXT("Need morePlayer To Ready"));
				//UE_LOG(LogTemp, Warning, TEXT("Need morePlayer To Ready"));
				RemainLobbyWaitingTimeForPlaying = LobbyWaitingTime; // 최소인원이 안된다면 대기 시간 초기화.
			}
			else
			{
				NotificationString = FString::Printf(TEXT("Wait %d seconds for playing."), RemainLobbyWaitingTimeForPlaying);
				//UE_LOG(LogTemp, Warning, TEXT("CountDown -1 sec"));
				--RemainLobbyWaitingTimeForPlaying;
			}
		}

		if (RemainLobbyWaitingTimeForPlaying <= 0)
		{
			NotificationString = FString::Printf(TEXT(""));
			MPGameState->MatchState = EMatchState::Playing;
		}

		NotifyToAllPlayer(NotificationString);

		break;
	}
	case EMatchState::Playing:
	{
		MPGameState->AlivePlayerContrReadyCountollerCount = SessionPlayerControllers.Num();

		FString NotificationString = FString::Printf(TEXT("All Player Ready, Game START!!"));

		NotifyToAllPlayer(NotificationString);

		RemainLobbyWaitingTimeForPlaying = LobbyWaitingTime; // 기다리는 시간 초기화
		MPGameState->MatchState = EMatchState::Ending;

		break;
	}

	case EMatchState::Ending:
	{
		FString NotificationString = FString::Printf(TEXT("Game Begins in %d"), RemainWaitingTimeForEnding);

		NotifyToAllPlayer(NotificationString);
		if (!IsMajorityReady())
		{// 중간에 누가 Ready를 품
			MPGameState->MatchState = EMatchState::Waiting;
			return;
		}


		--RemainWaitingTimeForEnding;

		if (RemainWaitingTimeForEnding <= 0)
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
		break;
	default:
		break;
	}
}

void AT6GameModeBase_Lobby::SetPlayerReady(APCController_GamePlay* PlayerController, bool bIsReady)
{
	if (IsValid(PlayerController) == false)
		return;

	LobbyPlayerReady.FindOrAdd(PlayerController) = bIsReady;

	int32 ReadyCount = 0;
	for (auto& Elem : LobbyPlayerReady)
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
	AT6GameStateBase_Lobby* MPGameState = GetGameState<AT6GameStateBase_Lobby>();
	if (!IsValid(MPGameState)) return;

	MPGameState->ReadyPlayerCount = ReadyCount;

	if (IsMajorityReady())
	{
		MPGameState->MatchState = EMatchState::Waiting;
	}
}

bool AT6GameModeBase_Lobby::IsMajorityReady() const
{
	int32 ReadyCount = 0;

	for (auto& Elem : LobbyPlayerReady)
	{
		if (Elem.Value)
		{
			ReadyCount++;
		}
	}

	return ReadyCount > (SessionPlayerControllers.Num() / 2);
}
