import { createSignal } from "solid-js";
import { mount } from "@pocketjs/framework/solid";
import { Text, View } from "@pocketjs/framework/solid/components";
import { VirtualList, type VirtualListHandle } from "@pocketjs/framework/virtual-list";
import { onFrame } from "@pocketjs/framework/solid/lifecycle";
import { BTN } from "@pocketjs/framework/input";
import { reportAppAction } from "@pocketjs/framework/host";

const entries = [
  "你好，Brick", "中文阅读", "方向键导航", "长列表滚动", "按钮即时反馈",
  "游戏翻译", "连续对话", "阅读历史", "字体与排版", "网络与配置",
  "应用开发", "下一步：AI 聊天",
];

function App() {
  const [chosen, setChosen] = createSignal("按 A 选择一项");
  const [count, setCount] = createSignal(0);
  const [position, setPosition] = createSignal(1);
  let list: VirtualListHandle | undefined;
  let lastState = -1;
  let heldDirection = 0;
  let heldFrames = 0;
  onFrame((buttons) => {
    if (!list) return;
    const direction = buttons & BTN.DOWN ? 1 : buttons & BTN.UP ? -1 : 0;
    if (direction !== heldDirection) { heldDirection = direction; heldFrames = 0; }
    else if (direction && ++heldFrames >= 24 && (heldFrames - 24) % 6 === 0) {
      list.focusRow((list.focusedIndex() ?? 0) + direction);
    }
    const index = list.focusedIndex() ?? 0;
    setPosition(index + 1);
    const state = index * 10000 + Math.round(list.scroller.offset());
    if (state !== lastState) {
      lastState = state;
      reportAppAction("list.state", state);
    }
  });
  const activate = (index: number) => {
    setChosen(entries[index]);
    setCount(count() + 1);
    reportAppAction("list.activate", index);
  };
  return (
    <View class="flex-col w-full h-full bg-[#151c18] p-[40] gap-[20]">
      <View class="flex-row items-center justify-between h-[78]">
        <Text class="text-5xl font-bold text-[#f3f1de]">口袋应用</Text>
        <Text class="text-2xl text-[#c1cbbf]">NextUI · Brick</Text>
      </View>
      <View class="flex-row items-center justify-between h-[40]">
        <Text class="text-2xl text-[#c1cbbf]">中文、导航与滚动</Text>
        <Text class="text-2xl text-[#c1cbbf]">{position()} / 12</Text>
      </View>
      <VirtualList count={entries.length} rowHeight={80} height={400} overscan={80}
        ref={(handle) => { list = handle; handle.focusRow(0); }} onRowPress={activate}
        renderRow={(index) => (
          <View class={position() === index + 1
            ? "flex-row items-center w-full h-[80] px-[24] gap-[24] bg-[#344d2e]"
            : "flex-row items-center w-full h-[80] px-[24] gap-[24] bg-[#151c18]"}>
            <Text class="text-2xl text-[#c1cbbf]">{index + 1}.</Text>
            <Text class="text-4xl text-[#f3f1de]">{entries[index]}</Text>
          </View>
        )}
      />
      <View class="flex-row justify-between items-center h-[44]">
        <Text class="text-2xl text-[#f3f1de]">{chosen()}</Text>
        <Text class="text-2xl text-[#c1cbbf]">已选择 {count()} 次</Text>
      </View>
      <Text class="text-2xl text-[#c1cbbf]">上下：移动　A：选择　B / MENU：退出</Text>
    </View>
  );
}
mount(() => <App />);
