import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { applyCollocationModelStatus, setupDropdownMenu } from './shared';

vi.mock('./theme', () => ({ setSurfaceTheme: vi.fn(), setThemeMode: vi.fn() }));

class MenuElement extends EventTarget {
  textContent = '';
  dataset: Record<string, string> = {};
  attributes = new Map<string, string>();
  classList = { add: vi.fn(), remove: vi.fn(), contains: vi.fn(() => false) };
  label?: MenuElement;
  children: MenuElement[] = [];
  tabIndex = 0;
  querySelector() { return this.label; }
  querySelectorAll() { return this.children; }
  contains() { return true; }
  closest() { return this; }
  getAttribute(name: string) { return this.attributes.get(name) ?? null; }
  setAttribute(name: string, value: string) { this.attributes.set(name, value); }
  focus() { (document as unknown as { activeElement: MenuElement }).activeElement = this; }
  click() {
    const event = new Event('click');
    Object.defineProperty(event, 'target', { value: this });
    this.parent?.dispatchEvent(event);
  }
  parent?: MenuElement;
}

let button: MenuElement;
let menu: MenuElement;
let item: MenuElement;
let label: MenuElement;
let postMessage: ReturnType<typeof vi.fn>;
beforeEach(() => {
  button = new MenuElement();
  label = new MenuElement();
  label.textContent = '[ / ]';
  button.label = label;
  menu = new MenuElement();
  item = new MenuElement();
  item.textContent = '- / =';
  item.dataset.value = 'minus_equal';
  menu.children = [item];
  item.parent = menu;
  postMessage = vi.fn();
  vi.stubGlobal('HTMLInputElement', class {});
  vi.stubGlobal('window', { chrome: { webview: { postMessage } } });
  vi.stubGlobal('document', {
    activeElement: null,
    getElementById: (id: string) => id === 'button' ? button : menu,
    addEventListener: vi.fn()
  });
  setupDropdownMenu('button', 'menu', '', true, 'input.word_to_character_keys');
});
afterEach(() => vi.unstubAllGlobals());

function clickItem() {
  const event = new Event('click');
  Object.defineProperty(event, 'target', { value: item });
  menu.dispatchEvent(event);
}

it('supports arrow and Enter keyboard selection through the shared dropdown handler', async () => {
  const arrow = new Event('keydown');
  Object.defineProperty(arrow, 'key', { value: 'ArrowDown' });
  button.dispatchEvent(arrow);
  await Promise.resolve();
  expect(document.activeElement).toBe(item);
  expect(item.getAttribute('role')).toBe('option');

  const enter = new Event('keydown');
  Object.defineProperty(enter, 'key', { value: 'Enter' });
  menu.dispatchEvent(enter);
  expect(label.textContent).toBe('- / =');
  expect(postMessage).toHaveBeenCalledTimes(1);
});

function pressKey(target: MenuElement, key: string) {
  const event = new Event('keydown');
  Object.defineProperty(event, 'key', { value: key });
  target.dispatchEvent(event);
}

function useItems(count: number): MenuElement[] {
  const items = Array.from({ length: count }, (_, index) => {
    const entry = new MenuElement();
    entry.textContent = `item ${index}`;
    entry.dataset.value = `value_${index}`;
    entry.parent = menu;
    return entry;
  });
  menu.children = items;
  return items;
}

it('enters a multi-item menu at the first item going down and the last going up', async () => {
  const items = useItems(4);
  (document as unknown as { activeElement: MenuElement }).activeElement = button;
  pressKey(button, 'ArrowDown');
  await Promise.resolve();
  expect(document.activeElement).toBe(items[0]);

  button.focus();
  pressKey(button, 'ArrowUp');
  await Promise.resolve();
  expect(document.activeElement).toBe(items[3]);
});

it('wraps arrow navigation inside the menu', () => {
  const items = useItems(3);
  items[2].focus();
  pressKey(menu, 'ArrowDown');
  expect(document.activeElement).toBe(items[0]);
  pressKey(menu, 'ArrowUp');
  expect(document.activeElement).toBe(items[2]);
});

it('ignores Enter when focus is not on a menu item', () => {
  useItems(2);
  // Were the focused element clicked blindly, this click would reach the
  // menu's delegated handler and be taken for an item selection.
  button.textContent = 'toggle';
  button.parent = menu;
  button.focus();
  pressKey(menu, 'Enter');
  expect(postMessage).not.toHaveBeenCalled();
  expect(label.textContent).toBe('[ / ]');
});

it('does not select or send a disabled dropdown item', () => {
  item.attributes.set('aria-disabled', 'true');
  clickItem();
  expect(label.textContent).toBe('[ / ]');
  expect(postMessage).not.toHaveBeenCalled();
  expect(menu.classList.remove).not.toHaveBeenCalled();
});

it('allows the item after the host clears its disabled state', () => {
  item.attributes.set('aria-disabled', 'true');
  clickItem();
  item.attributes.set('aria-disabled', 'false');
  clickItem();
  expect(label.textContent).toBe('- / =');
  expect(postMessage).toHaveBeenCalledTimes(1);
  const message = postMessage.mock.calls[0]?.[0];
  expect(typeof message === 'string' ? JSON.parse(message) : message).toMatchObject({
    type: 'configUpdate', data: { path: 'input.word_to_character_keys', value: 'minus_equal' }
  });
  expect(menu.classList.remove).toHaveBeenCalledWith('open');
});

describe('collocation model status gating', () => {
  const TOGGLE_IDS = ['sentenceCollocationToggleBtn'];
  const BUILT_IN_ID = 'wanxiang-lts-zh-hans';
  const CATALOG = [
    { id: BUILT_IN_ID, displayName: '万象 LTS（推荐）', sizeHint: '约 390 MB', license: 'CC-BY-4.0' },
    { id: 'zh-moqi', displayName: '白霜（实验）', sizeHint: '约 7 MB', license: 'GPL-3.0' }
  ];

  // 只实现 shared.ts 实际用到的那几个成员：目录行是动态种子的，测试里用这份假 DOM
  // 复刻 querySelector/dataset 的最小行为。
  class FakeElement {
    tagName: string;
    children: FakeElement[] = [];
    dataset: Record<string, string> = {};
    className = '';
    textContent = '';
    type = '';
    value = '';
    disabled = false;
    hidden = false;
    checked = false;
    tabIndex = 0;
    attributes = new Map<string, string>();
    handlers = new Map<string, () => void>();
    classes = new Set<string>();
    classList = {
      toggle: (name: string, on: boolean): boolean => {
        if (on) {
          this.classes.add(name);
        } else {
          this.classes.delete(name);
        }
        return on;
      }
    };

    constructor(tagName = 'div') {
      this.tagName = tagName;
    }

    appendChild(child: FakeElement): FakeElement {
      this.children.push(child);
      return child;
    }
    addEventListener(type: string, handler: () => void): void {
      this.handlers.set(type, handler);
    }
    setAttribute(name: string, value: string) {
      this.attributes.set(name, value);
    }
    getAttribute(name: string) {
      return this.attributes.get(name) ?? null;
    }
    querySelector(selector: string): FakeElement | null {
      const matches = (el: FakeElement): boolean => {
        // createElement 的传参在假 DOM 里保持原样（小写 'input'），真实 DOM 的 tagName
        // 是大写，这里两边通吃。
        if (selector === 'input[type="radio"]') return el.tagName.toUpperCase() === 'INPUT' && el.type === 'radio';
        if (selector.startsWith('.')) return el.className.split(' ').includes(selector.slice(1));
        return false;
      };
      for (const child of this.children) {
        if (matches(child)) return child;
        const nested = child.querySelector(selector);
        if (nested) return nested;
      }
      return null;
    }
  }

  let toggles: Record<string, FakeElement>;
  let toggleRows: FakeElement[];
  let modelList: FakeElement;
  let postMessage: ReturnType<typeof vi.fn>;

  const rows = (): FakeElement[] => modelList.children;
  const rowById = (id: string): FakeElement => rows().find((row) => row.dataset.modelId === id)!;
  const radioOf = (row: FakeElement): FakeElement => row.querySelector('input[type="radio"]')!;
  const downloadOf = (row: FakeElement): FakeElement => row.querySelector('.collocation-model-download')!;
  const removeOf = (row: FakeElement): FakeElement => row.querySelector('.collocation-model-delete')!;
  const statusOf = (row: FakeElement): FakeElement => row.querySelector('.collocation-model-status')!;

  beforeEach(() => {
    toggles = Object.fromEntries(TOGGLE_IDS.map((id) => [id, new FakeElement()]));
    toggleRows = [new FakeElement()];
    modelList = new FakeElement();
    postMessage = vi.fn();
    vi.stubGlobal('document', {
      createElement: (tag: string) => new FakeElement(tag),
      getElementById: (id: string) => (id === 'collocationModelList' ? modelList : toggles[id] ?? null),
      querySelectorAll: (selector: string) => {
        if (selector === '.collocation-toggle-row') return toggleRows;
        if (selector === '.collocation-model-row') return rows();
        return [];
      }
    });
    vi.stubGlobal('window', { chrome: { webview: { postMessage } } });
  });

  it('seeds one row per catalog entry and leaves the built-in package activatable while absent', () => {
    applyCollocationModelStatus({ [BUILT_IN_ID]: { state: 'absent' }, 'zh-moqi': { state: 'absent' } }, CATALOG, '');
    expect(rows()).toHaveLength(2);
    // 激活留空 = 内置推荐包：对应单选选中且可点（未下载也可用，引擎静默降级）。
    expect(radioOf(rowById(BUILT_IN_ID)).value).toBe('');
    expect(radioOf(rowById(BUILT_IN_ID)).disabled).toBe(false);
    expect(radioOf(rowById(BUILT_IN_ID)).checked).toBe(true);
    // 未就绪的社区包不可激活。
    expect(radioOf(rowById('zh-moqi')).disabled).toBe(true);
    expect(radioOf(rowById('zh-moqi')).checked).toBe(false);
    // 生效解析（留空 = 内置推荐）未就绪 → 整句开关置灰。
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('true');
      expect(toggles[id].tabIndex).toBe(-1);
    }
    expect(toggleRows.every((row) => row.classes.has('is-disabled'))).toBe(true);
  });

  it('does not duplicate rows across snapshots', () => {
    applyCollocationModelStatus({ 'zh-moqi': { state: 'absent' } }, CATALOG, '');
    applyCollocationModelStatus({ 'zh-moqi': { state: 'ready' } }, CATALOG, 'zh-moqi');
    expect(rows()).toHaveLength(2);
  });

  it('replays per-model status text and download/delete visibility', () => {
    applyCollocationModelStatus(
      { [BUILT_IN_ID]: { state: 'ready' }, 'zh-moqi': { state: 'downloading', progress: 40 } },
      CATALOG,
      ''
    );
    const builtIn = rowById(BUILT_IN_ID);
    const moqi = rowById('zh-moqi');
    expect(statusOf(builtIn).textContent).toBe('模型已就绪');
    expect(downloadOf(builtIn).disabled).toBe(true);
    // 当前生效解析（留空 = 内置推荐）的包不可删。
    expect(removeOf(builtIn).hidden).toBe(true);
    expect(statusOf(moqi).textContent).toBe('下载中 40%');
    expect(downloadOf(moqi).disabled).toBe(true);
    expect(removeOf(moqi).hidden).toBe(true);
  });

  it('activates a ready model, unguards the toggles, and offers deletion only for inactive rows', () => {
    applyCollocationModelStatus({ [BUILT_IN_ID]: { state: 'ready' }, 'zh-moqi': { state: 'ready' } }, CATALOG, 'zh-moqi');
    expect(radioOf(rowById('zh-moqi')).disabled).toBe(false);
    expect(radioOf(rowById('zh-moqi')).checked).toBe(true);
    expect(removeOf(rowById('zh-moqi')).hidden).toBe(true);
    expect(removeOf(rowById(BUILT_IN_ID)).hidden).toBe(false);
    // 生效解析 = zh-moqi 已就绪 → 整句开关解禁。
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('false');
      expect(toggles[id].tabIndex).toBe(0);
    }
    expect(toggleRows.every((row) => row.classes.has('is-disabled'))).toBe(false);
  });

  it('keeps the toggles disabled while the active model is downloading or failed', () => {
    applyCollocationModelStatus({ 'zh-moqi': { state: 'downloading', progress: 10 } }, CATALOG, 'zh-moqi');
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('true');
    }
    applyCollocationModelStatus({ 'zh-moqi': { state: 'error', error: '校验失败' } }, CATALOG, 'zh-moqi');
    expect(statusOf(rowById('zh-moqi')).textContent).toBe('下载失败：校验失败');
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('true');
    }
    // 失败后下载按钮放行，允许重试。
    expect(downloadOf(rowById('zh-moqi')).disabled).toBe(false);
  });

  it('disables the other rows download buttons while any download is in flight', () => {
    applyCollocationModelStatus(
      { [BUILT_IN_ID]: { state: 'absent' }, 'zh-moqi': { state: 'downloading', progress: 10 } },
      CATALOG,
      ''
    );
    expect(downloadOf(rowById('zh-moqi')).disabled).toBe(true);
    // 单网络槽：未下载的行也一并禁用，忙碌语义在界面上可见（服务端拒绝是兜底）。
    expect(downloadOf(rowById(BUILT_IN_ID)).disabled).toBe(true);
  });

  it('sends the activation update with an empty value for the built-in row', () => {
    applyCollocationModelStatus({ 'zh-moqi': { state: 'ready' } }, CATALOG, '');
    radioOf(rowById(BUILT_IN_ID)).handlers.get('change')?.();
    const message = JSON.parse(postMessage.mock.calls[0]?.[0] as string);
    expect(message).toMatchObject({
      type: 'configUpdate',
      data: { path: 'association.sentence_collocation_model', value: '' }
    });
  });

  it('sends download and delete requests with the row model id', () => {
    applyCollocationModelStatus({ 'zh-moqi': { state: 'absent' } }, CATALOG, '');
    downloadOf(rowById('zh-moqi')).handlers.get('click')?.();
    expect(JSON.parse(postMessage.mock.calls[0]?.[0] as string)).toMatchObject({
      type: 'collocationModelDownload',
      data: { modelId: 'zh-moqi' }
    });

    applyCollocationModelStatus({ 'zh-moqi': { state: 'ready' } }, CATALOG, '');
    removeOf(rowById('zh-moqi')).handlers.get('click')?.();
    expect(JSON.parse(postMessage.mock.calls[1]?.[0] as string)).toMatchObject({
      type: 'collocationModelDelete',
      data: { modelId: 'zh-moqi' }
    });
  });
});
