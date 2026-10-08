import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import partial from '../partials/voice.html?raw';

vi.mock('./shared', () => ({
  applyDropdownValue: vi.fn(),
  applyToggleState: vi.fn(),
  setupDropdownMenu: vi.fn(),
  setupToggleButton: vi.fn()
}));
vi.mock('./config-sync', () => ({ updateConfig: vi.fn() }));
vi.mock('./credential-test', () => ({ setupCredentialTest: vi.fn() }));

import { applyVoiceConfig, setupVoiceInput } from './voice';
import { updateConfig } from './config-sync';
import { setupCredentialTest } from './credential-test';

class StubElement {
  value = '';
  classes = new Set<string>();
  classList = {
    contains: (name: string) => this.classes.has(name),
    toggle: (name: string, enabled: boolean) => enabled ? this.classes.add(name) : this.classes.delete(name)
  };
  listeners = new Map<string, (event: unknown) => void>();
  addEventListener(type: string, listener: (event: unknown) => void): void {
    this.listeners.set(type, listener);
  }
  select(value: string): void {
    this.listeners.get('click')?.({ target: { closest: () => ({ dataset: { value } }) } });
  }
}

let elements: Map<string, StubElement>;

beforeEach(() => {
  vi.clearAllMocks();
  elements = new Map([
    'voiceAsrResourceId', 'voiceAsrResourceIdField', 'voiceAsrProviderMenu', 'voiceDoubaoAuthModeMenu'
  ].map(id => [id, new StubElement()]));
  vi.stubGlobal('document', {
    getElementById: (id: string) => elements.get(id) ?? null,
    querySelector: () => null,
    querySelectorAll: () => []
  });
  setupVoiceInput();
});

afterEach(() => vi.unstubAllGlobals());

it('exposes the existing hourly Resource ID default in the settings form', () => {
  expect(partial).toMatch(/<input\b[^>]*id="voiceAsrResourceId"/);
  expect(elements.get('voiceAsrResourceId')!.value).toBe('volc.seedasr.sauc.duration');
});

it('restores the default instead of saving or testing an empty Resource ID', () => {
  const resource = elements.get('voiceAsrResourceId')!;
  resource.value = '   ';
  const asrTest = vi.mocked(setupCredentialTest).mock.calls.find(([id]) => id === 'voiceAsrTestButton')!;
  expect(asrTest[3]()).toMatchObject({ resourceId: 'volc.seedasr.sauc.duration' });
  resource.listeners.get('change')?.({});
  expect(updateConfig).toHaveBeenCalledWith('voice_input.asr_resource_id', 'volc.seedasr.sauc.duration');
  expect(updateConfig).not.toHaveBeenCalledWith('voice_input.asr_resource_id', '');
  expect(resource.value).toBe('volc.seedasr.sauc.duration');
});

it('resets a stale Resource ID when the reloaded configuration lacks one', () => {
  applyVoiceConfig({ asr_provider: 'doubao', asr_resource_id: 'volc.bigasr.sauc.concurrent' });
  applyVoiceConfig({ asr_provider: 'doubao' });
  expect(elements.get('voiceAsrResourceId')!.value).toBe('volc.seedasr.sauc.duration');
});

it('loads and saves the Resource ID through the existing voice configuration key', () => {
  applyVoiceConfig({ asr_provider: 'doubao', asr_resource_id: 'volc.seedasr.sauc.concurrent' });
  const resource = elements.get('voiceAsrResourceId')!;
  expect(resource.value).toBe('volc.seedasr.sauc.concurrent');
  resource.value = ' volc.bigasr.sauc.duration ';
  resource.listeners.get('change')?.({});
  expect(updateConfig).toHaveBeenCalledWith('voice_input.asr_resource_id', 'volc.bigasr.sauc.duration');
});

it('tests the current Resource ID instead of the previous configuration snapshot', () => {
  applyVoiceConfig({ asr_provider: 'doubao', asr_resource_id: 'volc.seedasr.sauc.duration' });
  elements.get('voiceAsrResourceId')!.value = ' volc.seedasr.sauc.concurrent ';
  const asrTest = vi.mocked(setupCredentialTest).mock.calls.find(([id]) => id === 'voiceAsrTestButton')!;
  expect(asrTest[3]()).toMatchObject({ provider: 'doubao', resourceId: 'volc.seedasr.sauc.concurrent' });
});

it('shows the Resource ID for both Doubao auth modes and preserves it across provider switches', () => {
  applyVoiceConfig({ asr_provider: 'doubao', asr_resource_id: 'volc.seedasr.sauc.concurrent' });
  const field = elements.get('voiceAsrResourceIdField')!;
  expect(field.classList.contains('is-hidden')).toBe(false);
  elements.get('voiceDoubaoAuthModeMenu')!.select('legacy');
  expect(field.classList.contains('is-hidden')).toBe(false);
  elements.get('voiceAsrProviderMenu')!.select('openai');
  expect(field.classList.contains('is-hidden')).toBe(true);
  elements.get('voiceAsrProviderMenu')!.select('doubao');
  expect(field.classList.contains('is-hidden')).toBe(false);
  expect(elements.get('voiceAsrResourceId')!.value).toBe('volc.seedasr.sauc.concurrent');
});
