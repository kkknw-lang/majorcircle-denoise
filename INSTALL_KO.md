# MajorCircle Denoise v2 설치 및 사용 방법 (Logic Pro)

## 설치 (터미널에 한 번에 붙여넣기)
1. 다운로드한 `MajorCircle-Denoise-mac.zip`을 더블클릭해서 압축을 푸세요.
2. **터미널** 앱을 열고 아래 내용을 통째로 붙여넣은 뒤 Enter를 누르세요. 이전 버전이 있으면 새 버전으로 바뀌어요.

```
f=$(find ~/Downloads ~/Desktop -maxdepth 4 -name "MajorCircle Denoise.component" -print0 2>/dev/null | xargs -0 ls -dt 2>/dev/null | head -1); if [ -z "$f" ]; then echo "파일을 못 찾았어요"; else mkdir -p ~/Library/Audio/Plug-Ins/Components && rm -rf ~/Library/Audio/Plug-Ins/Components/"MajorCircle Denoise.component" && cp -R "$f" ~/Library/Audio/Plug-Ins/Components/ && xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"MajorCircle Denoise.component" && echo "설치 완료"; fi
```

3. Logic Pro를 완전히 껐다가 다시 켜고, **Audio FX → Audio Units → majorcircle → MajorCircle Denoise**를 고르세요.

> 이전 버전(v1)을 넣어둔 Logic 프로젝트는 설정값이 기본값으로 돌아가요. 조절 항목이 바뀌었기 때문이에요.

## 화면 보는 법
- **회색 영역 (Input)**: 들어오는 소리
- **주황 점선 (Noise threshold)**: 플러그인이 "이 아래는 노이즈"라고 판단하는 선
- **보라색 선 (Output)**: 노이즈를 줄인 뒤의 소리

## 조절 항목
| 이름 | 하는 일 | 팁 |
|---|---|---|
| **THRESHOLD** | 노이즈로 볼 기준의 높이 (dB) | 올리면 더 많이 지우고, 내리면 소리를 더 보존해요. 기본값은 3 dB예요. |
| **REDUCTION** | 노이즈를 최대 얼마나 줄일지 (dB) | 10~20 dB는 자연스럽고, 30 dB 이상은 강해요. |
| **SMOOTHING** | 처리의 부드러움 | 삐릭거리는 잔여음(뮤지컬 노이즈)이 들리면 올리세요. |
| **BAND REDUCTION** (60Hz ~ 16kHz, 8개) | 음역대마다 REDUCTION을 몇 % 적용할지 | 예: 험(웅~)은 60·150, 히스(쉬~)는 5k·10k·16k를 100%로 두고, 목소리 대역(400~2.5k)은 낮추세요. |
| **Listen to removed noise** | 지워지는 소리만 들려줘요 | 목소리가 섞여 들리면 THRESHOLD를 내리거나 해당 음역대를 낮추세요. |

## 노이즈 학습 (LEARN): 가장 강력한 방법
1. 노래나 말소리 없이 **노이즈만 있는 구간**을 재생하세요. 곡 앞부분 1~2초면 충분해요.
2. 재생 중에 **LEARN** 버튼을 누르면 빨간색으로 바뀌면서 학습을 시작해요.
3. 1초 이상 지난 뒤 **LEARN**을 다시 누르면 학습이 끝나요. 모드가 자동으로 **Learned**로 바뀌어요.
4. 학습한 노이즈는 **Logic 프로젝트에 함께 저장**돼요.

- **Auto 모드**: 최근 8초 동안의 소리로 노이즈를 계속 추정해요. 재생을 시작하고 0.5초쯤 지나야 효과가 걸려요.
- **Learned 모드**: 학습한 노이즈를 기준으로 지워요. 노이즈가 일정하다면 Auto보다 정확해요.

## 알아두면 좋은 점
- 지연(레이턴시)이 2048샘플(48kHz 기준 약 43ms)이에요. Logic이 자동으로 보정해줘요.
- 스테레오 트랙은 좌우를 같은 양만큼 줄여서 소리의 위치(스테레오 이미지)가 흔들리지 않아요.
- VST3 버전(`.vst3`)은 Logic에서는 쓰지 않아요. 다른 프로그램에서 쓰려면 `~/Library/Audio/Plug-Ins/VST3` 폴더에 넣으세요.
