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
  const TOGGLE_IDS = ['sentenceCollocationAdditiveToggleBtn', 'sentenceCollocationRerankToggleBtn'];

  // 只实现 shared.ts 实际用到的那几个成员，避免继承 MenuElement 时和它的
  // classList mock 打架。
  class RowElement {
    classes = new Set<string>();
    tabIndex = 0;
    attributes = new Map<string, string>();
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
    setAttribute(name: string, value: string) {
      this.attributes.set(name, value);
    }
    getAttribute(name: string) {
      return this.attributes.get(name) ?? null;
    }
  }

  let toggles: Record<string, RowElement>;
  let rows: RowElement[];
  let downloadButton: { disabled: boolean };
  let statusText: { textContent: string };

  beforeEach(() => {
    toggles = Object.fromEntries(TOGGLE_IDS.map((id) => [id, new RowElement()]));
    rows = [new RowElement(), new RowElement()];
    downloadButton = { disabled: false };
    statusText = { textContent: '' };
    vi.stubGlobal('document', {
      getElementById: (id: string) => {
        if (id === 'collocationModelDownloadBtn') return downloadButton;
        if (id === 'collocationModelStatusText') return statusText;
        return toggles[id] ?? null;
      },
      querySelectorAll: (selector: string) => (selector === '.collocation-toggle-row' ? rows : [])
    });
    vi.stubGlobal('window', { chrome: { webview: { postMessage: vi.fn() } } });
  });

  it('grays out both toggles until the model is ready', () => {
    applyCollocationModelStatus({ state: 'absent' });
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('true');
      expect(toggles[id].tabIndex).toBe(-1);
    }
    expect(rows.every((row) => row.classes.has('is-disabled'))).toBe(true);
    expect(downloadButton.disabled).toBe(false);

    applyCollocationModelStatus({ state: 'ready' });
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('false');
      expect(toggles[id].tabIndex).toBe(0);
    }
    expect(rows.every((row) => row.classes.has('is-disabled'))).toBe(false);
  });

  it('keeps the toggles disabled while downloading and after a failure', () => {
    applyCollocationModelStatus({ state: 'downloading', progress: 40 });
    expect(downloadButton.disabled).toBe(true);
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('true');
    }

    applyCollocationModelStatus({ state: 'error', error: '校验失败' });
    expect(statusText.textContent).toBe('下载失败：校验失败');
    expect(downloadButton.disabled).toBe(false);
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('true');
    }
  });

  it('treats a missing status as not downloaded', () => {
    applyCollocationModelStatus(undefined);
    expect(statusText.textContent).toBe('未下载');
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('true');
    }
  });

  it('marks a ready model whose digest differs from the reviewed one', () => {
    applyCollocationModelStatus({ state: 'ready', reviewed: false });
    expect(statusText.textContent).toBe('模型已就绪（上游已更新，未经评测）');
    for (const id of TOGGLE_IDS) {
      expect(toggles[id].getAttribute('aria-disabled')).toBe('false');
    }

    applyCollocationModelStatus({ state: 'ready', reviewed: true });
    expect(statusText.textContent).toBe('模型已就绪');
  });
});
