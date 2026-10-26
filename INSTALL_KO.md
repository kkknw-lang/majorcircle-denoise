# MajorCircle Denoise 설치 방법 (Logic Pro)

## 1. 파일 복사
1. 압축을 풀면 `MajorCircle Denoise.component` 파일이 있어요.
2. Finder에서 `Command + Shift + G`를 누르고 아래 주소를 붙여넣은 뒤 Enter를 누르세요.

   ```
   ~/Library/Audio/Plug-Ins/Components
   ```
3. 열린 폴더에 `MajorCircle Denoise.component`를 끌어다 놓으세요.

## 2. 보안 해제 (한 번만)
인터넷에서 받은 파일이라 macOS가 막을 수 있어요.
**터미널** 앱(Spotlight에서 "터미널" 검색)을 열고 아래 한 줄을 붙여넣은 뒤 Enter를 누르세요.

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"MajorCircle Denoise.component"
```

## 3. Logic에서 사용하기
1. Logic Pro를 완전히 껐다가 다시 켜세요.
2. 트랙의 Audio FX 슬롯을 누르고 **Audio Units → majorcircle → MajorCircle Denoise**를 고르세요.
3. 메뉴에 보이지 않으면 **Logic Pro → 설정 → 플러그인 관리자**에서 `MajorCircle Denoise`를 찾아 **"선택 항목 재설정 및 다시 스캔"**을 누르세요.

## 조절 항목
- **Strength (강도)**: 0~100%. 웹사이트의 노이즈 제거 "강도"와 같은 값이에요.
- **Listen to removed noise**: 켜면 *지워지는 소리만* 들려요. 목소리가 같이 지워지는지 확인할 때 쓰세요.

## 알아두면 좋은 점
- 지연(레이턴시)이 2048샘플(48kHz 기준 약 43ms)이에요. Logic이 자동으로 보정해줘요.
- 재생을 시작하고 0.5초쯤 지나야 효과가 걸리기 시작해요. 최근 8초 동안의 소리를 듣고 노이즈 수준을 계속 다시 계산하기 때문이에요.
- 일정하게 깔리는 소리(히스, 험, 방 울림 소음)에 잘 맞아요. 목소리와 같은 대역에 섞인 소음은 잘 안 지워져요.
- VST3 버전(`.vst3`)은 Logic에서는 쓰지 않아요. 다른 프로그램(Cubase, Ableton 등)에서 쓰려면 `~/Library/Audio/Plug-Ins/VST3` 폴더에 넣으세요.
