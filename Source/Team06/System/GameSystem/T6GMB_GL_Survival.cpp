// Fill out your copyright notice in the Description page of Project Settings.


#include "System/GameSystem/T6GMB_GL_Survival.h"
#include "System/GameSystem/T6GSB_GL_Survival.h"
#include "Player/Controller/PCController_GamePlay.h"
#include "Player/Etc/DeathTriggerComponent.h"
#include "Engine/PlayerStartPIE.h"
#include "Player/PlayerState/PlayerCharacterState.h"
void AT6GMB_GL_Survival::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	AT6GSB_GL_Survival* GSB_Survival = GetGameState<AT6GSB_GL_Survival>();
	if (IsValid(GSB_Survival) == false)
	{
		return;
	}
	APCController_GamePlay* NewPlayerController = Cast<APCController_GamePlay>(NewPlayer);
}

void AT6GMB_GL_Survival::Logout(AController* Exiting)
{
	Super::Logout(Exiting);

	APCController_GamePlay* ExitingPlayerController = Cast<APCController_GamePlay>(Exiting);
	if (IsValid(ExitingPlayerController) == true && AlivePlayerControllers.Find(ExitingPlayerController) != INDEX_NONE)
	{
		AlivePlayerControllers.Remove(ExitingPlayerController);
		DeadPlayerControllers.Add(ExitingPlayerController);
	}
	
}

void AT6GMB_GL_Survival::BeginPlay()
{
	Super::BeginPlay();

	AT6GSB_GL_Survival* GSB_S = GetGameState<AT6GSB_GL_Survival>();
	if (GSB_S)
	{
		//GSB_S->InitiatePlayerLife(SurvivalGamePlayerLife);
		GSB_S->InitSpawnPoint();
	}

}

void AT6GMB_GL_Survival::StartPlay()
{
	Super::StartPlay();
	AT6GSB_GL_Survival* GSB_S = GetGameState<AT6GSB_GL_Survival>();
	if (!GSB_S)
	{
		UE_LOG(LogTemp, Error, TEXT("GameStateBase가 유효하지 않습니다."));
		return;
	}
	else
	{
		GSB_S->InitSpawnPoint();
	}
}

void AT6GMB_GL_Survival::OnCharacterDead(AController* InController)
{
	if (IsValid(InController) == false)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invaild Character Death Called"));
		return;
	}
	if (!AlivePlayerControllers.Contains(InController))
	{
		UE_LOG(LogTemp, Warning, TEXT("Invaild AlivePlayerControllers Death Called"));
		return;
	}
	AT6GSB_GL_Survival* GSB_Survival = GetGameState<AT6GSB_GL_Survival>();

	if (IsValid(GSB_Survival) == true)
	{
		if (GSB_Survival->PlayerLives.Contains(InController))
		{
			if (GSB_Survival->PlayerLives[InController] <= 0)
			{
				AlivePlayerControllers.Remove(InController);
				DeadPlayerControllers.Add(InController);

				// 죽은 플레이어 UI 및 Spector처리하기
				HandleDeathController(InController);
			}
			else
			{
				--GSB_Survival->PlayerLives[InController];
				UE_LOG(LogTemp, Warning, TEXT("Player %s Dead, Reaspawning... "),*InController->PlayerState->GetPlayerName());
				APawn* PlayerPawn = InController->GetPawn();

				if (InController->IsPlayerController())
				{
					if (PlayerPawn)
					{
						// Pawn 숨기고 충돌 제거해서 폐기처럼 처리 (BUT 유지)
						PlayerPawn->SetActorEnableCollision(false);
						PlayerPawn->SetActorHiddenInGame(true);
						PlayerPawn->DisableInput(Cast<APlayerController>(InController));

						// 위치 보관하고 Spectator 전환
						GSB_Survival->RespawningPawn.Add(InController, PlayerPawn);
						// 컨트롤러 언포제션 (중요: 소유권 유지하면 Spectator로 안 넘어감)
						InController->UnPossess();
						if (InController->GetPawn())
						{
							UE_LOG(LogTemp, Warning, TEXT("Error, Player Still Posses Pawn"));
						}
					}
					Cast<APlayerController>(InController)->StartSpectatingOnly();

				}
				
				// 관전 모드 전환
				
				if (APCController_GamePlay* PCGP = Cast<APCController_GamePlay>(InController))
				{
					GetWorld()->GetTimerManager().SetTimer(
						PCGP->RespawnHandle,
						FTimerDelegate::CreateUObject(
							GSB_Survival,
							&AT6GSB_GL_Survival::RespawnPlayer,
							InController
						),
						RespawnWaitingTime,
						false
					);

					PCGP->Server_StartRespawnUI(RespawnWaitingTime);
				}

			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("No Controller Available at OnCharacterDeath"));
			return;
		}
		GSB_Survival->AliveSurviverCount = AlivePlayerControllers.Num();

		// 혼자 남은 경우 승리처리 델리게이트 실행
		if (GSB_Survival->AliveSurviverCount <= 1)
		{
			HandlePlayerGameWin(AlivePlayerControllers[0]);
		}
	}
}

void AT6GMB_GL_Survival::RegistPCSessionToAlive()
{
	for (APCController_GamePlay* PCC_GP : SessionPlayerControllers)
	{
		if (!AlivePlayerControllers.Contains(PCC_GP))
		{
			AlivePlayerControllers.Add(PCC_GP);
			UE_LOG(LogTemp, Warning, TEXT(" ControllerRegistered"));
		}

	}
	return;
}

void AT6GMB_GL_Survival::HandleDeathController(AController* InController)
{
	if (!InController) return;
	AT6GSB_GL_Survival* GSB_Survival = GetGameState<AT6GSB_GL_Survival>();
	APawn* PlayerPawn = InController->GetPawn();
	DeadPlayerControllers.Add(InController);
	AlivePlayerControllers.Remove(InController);
	if (InController->IsPlayerController())
	{
		if (PlayerPawn)
		{
			// Pawn 숨기고 충돌 제거해서 폐기처럼 처리 (BUT 유지)
			PlayerPawn->SetActorEnableCollision(false);
			PlayerPawn->SetActorHiddenInGame(true);
			PlayerPawn->DisableInput(Cast<APlayerController>(InController));

			// 위치 보관하고 Spectator 전환
			GSB_Survival->RespawningPawn.Add(InController, PlayerPawn);

		}
		else
		{
			UE_LOG(LogTemp,Warning,TEXT("Pawn Nullptr before Erase"))
		}
		// 컨트롤러 언포제션 (중요: 소유권 유지하면 Spectator로 안 넘어감)
		InController->UnPossess();
		Cast<APlayerController>(InController)->StartSpectatingOnly();

		// 관전 모드 전환

	}
}

void AT6GMB_GL_Survival::EndingSurvivalGameLevel()
{
	for (AController* AlivePlayer : AlivePlayerControllers)
	{

	}
	
	for (AController* DeadPlayer : DeadPlayerControllers)
	{

	}
}

void AT6GMB_GL_Survival::OnMainTimerElapsed()
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

		AT6GSB_GL_Survival* SurvivalGameState = Cast<AT6GSB_GL_Survival>(MPGameState);
		if (SurvivalGameState)
		{
			SurvivalGameState->InitiatePlayerLife(SurvivalGamePlayerLife);
		}
		RegistPCSessionToAlive();
		
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
		RemainGameLevelWaitingTimeForPlaying = GameLevelWaitingTime; // 기다리는 시간 초기화
		
		AT6GSB_GL_Survival* SurvivalGameState = Cast<AT6GSB_GL_Survival>(MPGameState);
		if (SurvivalGameState)
		{
			SurvivalGameState->AliveSurviverCount = AlivePlayerControllers.Num();
			FString NotificationString = FString::Printf(TEXT("%d / %d"), SurvivalGameState->AliveSurviverCount, SessionPlayerControllers.Num());

			NotifyToAllPlayer(NotificationString);

		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Cast Faild from GameStatebase to GSB_GL_Survival"));
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
