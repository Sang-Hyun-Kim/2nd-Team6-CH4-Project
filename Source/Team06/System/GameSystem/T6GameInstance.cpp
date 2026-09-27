// Fill out your copyright notice in the Description page of Project Settings.


#include "System/GameSystem/T6GameInstance.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "HAL/PlatformProperties.h" // [5.8 migration] RequiresCookedData
#include "System/GameSystem/T6GameModeBase.h"
#include "System/GameSystem/T6GameStateBase.h"
void UT6GameInstance::Init()
{
	Super::Init();

	LoadLevelsFromFolder();

	InitAIPlayer();
}

FString UT6GameInstance::GetRandomLevel()
{
	if (LevelList.Num() == 0)
	{
		return TEXT(""); // 레벨이 없을 경우 빈 문자열 반환
	}

	int32 RandomIndex = FMath::RandRange(0, LevelList.Num() - 1);
	return LevelList[RandomIndex];
	// 중복 없는 것으로 수정필요
}

void UT6GameInstance::LoadLevelsFromFolder()
{
	LevelList.Empty();

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FString> PathsToScan = { "/Game/Team6/GameSystem/GS_Level/GameLevel" };
	// [5.8 migration] 쿠킹된 빌드(IoStore)에서는 .umap이 파일로 보이지 않아, bForceRescan=true 스캔이
	// 프리메이드 AssetRegistry의 GameLevel 항목을 "디스크에 없음"으로 지워 버림 → "No levels found".
	// 에디터(쿠킹 안 된 콘텐츠)에서만 스캔하고, 쿠킹된 빌드는 프리메이드 AssetRegistry를 그대로 사용한다.
	if (!FPlatformProperties::RequiresCookedData())
	{
		AssetRegistryModule.Get().ScanPathsSynchronous(PathsToScan, true);
	}
	TArray<FAssetData> AssetData;
	AssetRegistryModule.Get().GetAssetsByPath(FName("/Game/Team6/GameSystem/GS_Level/GameLevel"), AssetData, true);

	if (AssetData.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("No Assets found in /Game/Team6/GameSystem/GS_Level/GameLevel!"));
	}
	for (const FAssetData& Asset : AssetData)
	{
		FName AssetClassPathString = Asset.AssetClassPath.GetAssetName();

		if (AssetClassPathString == FName("World"))
		{
			FString LevelPath = Asset.GetObjectPathString();
			UE_LOG(LogTemp, Warning, TEXT("Found Level: %s"), *LevelPath);

			LevelPath = LevelPath.LeftChop(LevelPath.Find(TEXT(".")) - LevelPath.Len());
			int32 DotIndex;
			if (LevelPath.FindLastChar(TEXT('.'), DotIndex))
			{
				LevelPath = LevelPath.Left(DotIndex);  // 마지막 '.' 이전까지만 남김
			}
			LevelList.Add(LevelPath);
		}
	}

	// LevelList.Add("/Game/Team6/GameSystem/GS_Level/GameLevel/TempleLD");
	if (LevelList.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No levels found in /Game/Team6/GameSystem/GS_Level/GameLevel !!"));
	}
}

void UT6GameInstance::CheckWinScore()
{
	for (auto PlayerScore : PlayerScoreBoard)
	{
		if (PlayerScore.Value.TotalWins == GameOverScoreToWin)
		{
			FString WinnerName = PlayerScore.Value.PlayerName;
			FinalWinnerController = WinnerName;
			UE_LOG(LogTemp, Error, TEXT("%s has Win This Game!!!  % s has Win This Game!!! Game is Over Now!!!!!!!"), *WinnerName,*WinnerName);

			AT6GameModeBase* GMB = GetWorld()->GetAuthGameMode<AT6GameModeBase>();
			if (GMB)
			{
				GMB->bIsGameFinallyFinished = true;
				GMB->GetGameState<AT6GameStateBase>()->MatchState = EMatchState::End;
			}
		}
	}
}

void UT6GameInstance::RegisterPlayer(const FString& PlayerName)
{
	if (!PlayerScoreBoard.Contains(PlayerName))
	{
		FPlayerScore NewEntry;
		NewEntry.PlayerName = PlayerName;
		PlayerScoreBoard.Add(PlayerName, NewEntry);
	}
	else
	{// 이미 있는 플레이어

	}
}

void UT6GameInstance::AddWinForPlayer(const FString& PlayerName)
{
	if (FPlayerScore* Entry = PlayerScoreBoard.Find(PlayerName))
	{
		Entry->TotalWins++;
		CheckWinScore();
	}
}

void UT6GameInstance::SetPlayerSkinName(FString PlayerName, FName SkinName)
{
	
	if (PlayerScoreBoard.Contains(PlayerName))
	{
		PlayerScoreBoard[PlayerName].SkinName = SkinName;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Can't Set PlayerSkinName"));
	}
}

void UT6GameInstance::PrintScoreBoardLog()
{
	UE_LOG(LogTemp, Warning, TEXT("Current Scoreboard"));
	for (auto PS : PlayerScoreBoard)
	{
		UE_LOG(LogTemp, Warning, TEXT("Player %s : %d "),*PS.Key,PS.Value.TotalWins);
	}
}

TArray<FPlayerScoreEntry> UT6GameInstance::PackingPlayerScore()
{
	UE_LOG(LogTemp, Warning, TEXT("Packing PlayerScores"));
	TArray<FPlayerScoreEntry> ScoreDatas;
	for (TTuple<FString, FPlayerScore> PlayerScore : PlayerScoreBoard)
	{
		FPlayerScoreEntry PlayerScoreEntry;
		PlayerScoreEntry.PlayerName = PlayerScore.Key;
		PlayerScoreEntry.Score = PlayerScore.Value.TotalWins;
		ScoreDatas.Add(PlayerScoreEntry);
	}
	UE_LOG(LogTemp, Warning, TEXT("Successfully Packed PlayerScores"));
	return ScoreDatas;
}

void UT6GameInstance::InitAIPlayer()
{
	// AI 두 명 하드코딩
	FPlayerScore NewEntry1;
	NewEntry1.PlayerName = "Bixby";
	AIScoreBoard.Add(FString("Bixby"), NewEntry1);
	FPlayerScore NewEntry2;
	NewEntry2.PlayerName = "Siri";
	AIScoreBoard.Add(FString("Siri"), NewEntry2);
}

void UT6GameInstance::AddAIPlayer()
{
	if (CurrentAINum >= MaxAIAllow)
	{
		UE_LOG(LogTemp, Warning, TEXT("No More AIControllers Allowed"));
		return;
	}
	CurrentAINum+=2;
	bIsAISpawned = true;
}
