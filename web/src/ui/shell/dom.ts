/// A small typed helper for building DOM without innerHTML (nothing in
/// this app inserts markup from a string, so there is nothing to
/// sanitise). Attributes are set as attributes, text as text.

type Child = Node | string | null | undefined | false;

export interface ElementProps {
  class?: string;
  text?: string;
  attrs?: Record<string, string>;
  /// Properties set directly (`type`, `hidden`, `disabled`, `value`...).
  props?: Record<string, string | number | boolean>;
}

export function el<K extends keyof HTMLElementTagNameMap>(tag: K, props: ElementProps = {}, ...children: Child[]): HTMLElementTagNameMap[K] {
  const element = document.createElement(tag);
  if (props.class) element.className = props.class;
  if (props.text !== undefined) element.textContent = props.text;
  for (const [name, value] of Object.entries(props.attrs ?? {})) element.setAttribute(name, value);
  for (const [name, value] of Object.entries(props.props ?? {})) (element as unknown as Record<string, unknown>)[name] = value;
  for (const child of children) {
    if (child === null || child === undefined || child === false) continue;
    element.append(child);
  }
  return element;
}
