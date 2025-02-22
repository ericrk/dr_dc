/*
 * Copyright (C) 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @implements {SDK.TargetManager.Observer}
 * @unrestricted
 */
export default class Linkifier {
  /**
   * @param {number=} maxLengthForDisplayedURLs
   * @param {boolean=} useLinkDecorator
   */
  constructor(maxLengthForDisplayedURLs, useLinkDecorator) {
    this._maxLength = maxLengthForDisplayedURLs || UI.MaxLengthForDisplayedURLs;
    /** @type {!Map<!SDK.Target, !Array<!Element>>} */
    this._anchorsByTarget = new Map();
    /** @type {!Map<!SDK.Target, !Bindings.LiveLocationPool>} */
    this._locationPoolByTarget = new Map();
    this._useLinkDecorator = !!useLinkDecorator;
    _instances.add(this);
    SDK.targetManager.observeTargets(this);
  }

  /**
   * @param {!LinkDecorator} decorator
   */
  static setLinkDecorator(decorator) {
    console.assert(!_decorator, 'Cannot re-register link decorator.');
    _decorator = decorator;
    decorator.addEventListener(LinkDecorator.Events.LinkIconChanged, onLinkIconChanged);
    for (const linkifier of _instances) {
      linkifier._updateAllAnchorDecorations();
    }

    /**
     * @param {!Common.Event} event
     */
    function onLinkIconChanged(event) {
      const uiSourceCode = /** @type {!Workspace.UISourceCode} */ (event.data);
      const links = uiSourceCode[_sourceCodeAnchors] || [];
      for (const link of links) {
        Linkifier._updateLinkDecorations(link);
      }
    }
  }

  _updateAllAnchorDecorations() {
    for (const anchors of this._anchorsByTarget.values()) {
      for (const anchor of anchors) {
        Linkifier._updateLinkDecorations(anchor);
      }
    }
  }

  /**
   * @param {!Element} anchor
   * @param {!Workspace.UILocation} uiLocation
   */
  static _bindUILocation(anchor, uiLocation) {
    Linkifier._linkInfo(anchor).uiLocation = uiLocation;
    if (!uiLocation) {
      return;
    }
    const uiSourceCode = uiLocation.uiSourceCode;
    let sourceCodeAnchors = uiSourceCode[_sourceCodeAnchors];
    if (!sourceCodeAnchors) {
      sourceCodeAnchors = new Set();
      uiSourceCode[_sourceCodeAnchors] = sourceCodeAnchors;
    }
    sourceCodeAnchors.add(anchor);
  }

  /**
   * @param {!Element} anchor
   */
  static _unbindUILocation(anchor) {
    const info = Linkifier._linkInfo(anchor);
    if (!info.uiLocation) {
      return;
    }

    const uiSourceCode = info.uiLocation.uiSourceCode;
    info.uiLocation = null;
    const sourceCodeAnchors = uiSourceCode[_sourceCodeAnchors];
    if (sourceCodeAnchors) {
      sourceCodeAnchors.delete(anchor);
    }
  }

  /**
   * @override
   * @param {!SDK.Target} target
   */
  targetAdded(target) {
    this._anchorsByTarget.set(target, []);
    this._locationPoolByTarget.set(target, new Bindings.LiveLocationPool());
  }

  /**
   * @override
   * @param {!SDK.Target} target
   */
  targetRemoved(target) {
    const locationPool = /** @type {!Bindings.LiveLocationPool} */ (this._locationPoolByTarget.remove(target));
    locationPool.disposeAll();
    const anchors = this._anchorsByTarget.remove(target);
    for (const anchor of anchors) {
      const info = Linkifier._linkInfo(anchor);
      info.liveLocation = null;
      Linkifier._unbindUILocation(anchor);
      if (info.fallback) {
        anchor.href = info.fallback.href;
        anchor.title = info.fallback.title;
        anchor.className = info.fallback.className;
        anchor.textContent = info.fallback.textContent;
        anchor[_infoSymbol] = info.fallback[_infoSymbol];
      }
    }
  }

  /**
   * @param {?SDK.Target} target
   * @param {?string} scriptId
   * @param {string} sourceURL
   * @param {number} lineNumber
   * @param {number=} columnNumber
   * @param {string=} classes
   * @return {?Element}
   */
  maybeLinkifyScriptLocation(target, scriptId, sourceURL, lineNumber, columnNumber, classes) {
    let fallbackAnchor = null;
    if (sourceURL) {
      fallbackAnchor = Linkifier.linkifyURL(
          sourceURL,
          {className: classes, lineNumber: lineNumber, columnNumber: columnNumber, maxLength: this._maxLength});
    }
    if (!target || target.isDisposed()) {
      return fallbackAnchor;
    }
    const debuggerModel = target.model(SDK.DebuggerModel);
    if (!debuggerModel) {
      return fallbackAnchor;
    }

    const rawLocation =
        (scriptId ? debuggerModel.createRawLocationByScriptId(scriptId, lineNumber, columnNumber || 0) : null) ||
        debuggerModel.createRawLocationByURL(sourceURL, lineNumber, columnNumber || 0);
    if (!rawLocation) {
      return fallbackAnchor;
    }

    const anchor = Linkifier._createLink('', classes || '');
    const info = Linkifier._linkInfo(anchor);
    info.enableDecorator = this._useLinkDecorator;
    info.fallback = fallbackAnchor;
    info.liveLocation = Bindings.debuggerWorkspaceBinding.createLiveLocation(
        rawLocation, this._updateAnchor.bind(this, anchor),
        /** @type {!Bindings.LiveLocationPool} */ (this._locationPoolByTarget.get(rawLocation.debuggerModel.target())));

    const anchors = /** @type {!Array<!Element>} */ (this._anchorsByTarget.get(rawLocation.debuggerModel.target()));
    anchors.push(anchor);
    return anchor;
  }

  /**
   * @param {?SDK.Target} target
   * @param {?string} scriptId
   * @param {string} sourceURL
   * @param {number} lineNumber
   * @param {number=} columnNumber
   * @param {string=} classes
   * @return {!Element}
   */
  linkifyScriptLocation(target, scriptId, sourceURL, lineNumber, columnNumber, classes) {
    const scriptLink = this.maybeLinkifyScriptLocation(target, scriptId, sourceURL, lineNumber, columnNumber, classes);
    return scriptLink ||
        Linkifier.linkifyURL(
            sourceURL,
            {className: classes, lineNumber: lineNumber, columnNumber: columnNumber, maxLength: this._maxLength});
  }

  /**
   * @param {!SDK.DebuggerModel.Location} rawLocation
   * @param {string} fallbackUrl
   * @param {string=} classes
   * @return {!Element}
   */
  linkifyRawLocation(rawLocation, fallbackUrl, classes) {
    return this.linkifyScriptLocation(
        rawLocation.debuggerModel.target(), rawLocation.scriptId, fallbackUrl, rawLocation.lineNumber,
        rawLocation.columnNumber, classes);
  }

  /**
   * @param {?SDK.Target} target
   * @param {!Protocol.Runtime.CallFrame} callFrame
   * @param {string=} classes
   * @return {?Element}
   */
  maybeLinkifyConsoleCallFrame(target, callFrame, classes) {
    return this.maybeLinkifyScriptLocation(
        target, callFrame.scriptId, callFrame.url, callFrame.lineNumber, callFrame.columnNumber, classes);
  }

  /**
   * @param {!SDK.Target} target
   * @param {!Protocol.Runtime.StackTrace} stackTrace
   * @param {string=} classes
   * @return {!Element}
   */
  linkifyStackTraceTopFrame(target, stackTrace, classes) {
    console.assert(stackTrace.callFrames && stackTrace.callFrames.length);

    const topFrame = stackTrace.callFrames[0];
    const fallbackAnchor = Linkifier.linkifyURL(topFrame.url, {
      className: classes,
      lineNumber: topFrame.lineNumber,
      columnNumber: topFrame.columnNumber,
      maxLength: this._maxLength
    });
    if (target.isDisposed()) {
      return fallbackAnchor;
    }

    const debuggerModel = target.model(SDK.DebuggerModel);
    const rawLocations = debuggerModel.createRawLocationsByStackTrace(stackTrace);
    if (rawLocations.length === 0) {
      return fallbackAnchor;
    }

    const anchor = Linkifier._createLink('', classes || '');
    const info = Linkifier._linkInfo(anchor);
    info.enableDecorator = this._useLinkDecorator;
    info.fallback = fallbackAnchor;
    info.liveLocation = Bindings.debuggerWorkspaceBinding.createStackTraceTopFrameLiveLocation(
        rawLocations, this._updateAnchor.bind(this, anchor),
        /** @type {!Bindings.LiveLocationPool} */ (this._locationPoolByTarget.get(target)));

    const anchors = /** @type {!Array<!Element>} */ (this._anchorsByTarget.get(target));
    anchors.push(anchor);
    return anchor;
  }

  /**
   * @param {!SDK.CSSLocation} rawLocation
   * @param {string=} classes
   * @return {!Element}
   */
  linkifyCSSLocation(rawLocation, classes) {
    const anchor = Linkifier._createLink('', classes || '');
    const info = Linkifier._linkInfo(anchor);
    info.enableDecorator = this._useLinkDecorator;
    info.liveLocation = Bindings.cssWorkspaceBinding.createLiveLocation(
        rawLocation, this._updateAnchor.bind(this, anchor),
        /** @type {!Bindings.LiveLocationPool} */ (this._locationPoolByTarget.get(rawLocation.cssModel().target())));

    const anchors = /** @type {!Array<!Element>} */ (this._anchorsByTarget.get(rawLocation.cssModel().target()));
    anchors.push(anchor);
    return anchor;
  }

  reset() {
    for (const target of this._anchorsByTarget.keysArray()) {
      this.targetRemoved(target);
      this.targetAdded(target);
    }
  }

  dispose() {
    for (const target of this._anchorsByTarget.keysArray()) {
      this.targetRemoved(target);
    }
    SDK.targetManager.unobserveTargets(this);
    _instances.delete(this);
  }

  /**
   * @param {!Element} anchor
   * @param {!Bindings.LiveLocation} liveLocation
   */
  _updateAnchor(anchor, liveLocation) {
    Linkifier._unbindUILocation(anchor);
    const uiLocation = liveLocation.uiLocation();
    if (!uiLocation) {
      return;
    }

    Linkifier._bindUILocation(anchor, uiLocation);
    const text = uiLocation.linkText(true /* skipTrim */);
    Linkifier._setTrimmedText(anchor, text, this._maxLength);

    let titleText = uiLocation.uiSourceCode.url();
    if (typeof uiLocation.lineNumber === 'number') {
      titleText += ':' + (uiLocation.lineNumber + 1);
    }
    anchor.title = titleText;
    anchor.classList.toggle('webkit-html-blackbox-link', liveLocation.isBlackboxed());
    Linkifier._updateLinkDecorations(anchor);
  }

  /**
   * @param {!Element} anchor
   */
  static _updateLinkDecorations(anchor) {
    const info = Linkifier._linkInfo(anchor);
    if (!info || !info.enableDecorator) {
      return;
    }
    if (!_decorator || !info.uiLocation) {
      return;
    }
    if (info.icon && info.icon.parentElement) {
      anchor.removeChild(info.icon);
    }
    const icon = _decorator.linkIcon(info.uiLocation.uiSourceCode);
    if (icon) {
      icon.style.setProperty('margin-right', '2px');
      anchor.insertBefore(icon, anchor.firstChild);
    }
    info.icon = icon;
  }

  /**
   * @param {string} url
   * @param  {!Components.LinkifyURLOptions=} options
   * @return {!Element}
   */
  static linkifyURL(url, options) {
    options = options || {};
    const text = options.text;
    const className = options.className || '';
    const lineNumber = options.lineNumber;
    const columnNumber = options.columnNumber;
    const preventClick = options.preventClick;
    const maxLength = options.maxLength || UI.MaxLengthForDisplayedURLs;
    if (!url || url.trim().toLowerCase().startsWith('javascript:')) {
      const element = createElementWithClass('span', className);
      element.textContent = text || url || Common.UIString('(unknown)');
      return element;
    }

    let linkText = text || Bindings.displayNameForURL(url);
    if (typeof lineNumber === 'number' && !text) {
      linkText += ':' + (lineNumber + 1);
    }
    const title = linkText !== url ? url : '';
    const link = Linkifier._createLink(linkText, className, maxLength, title, url, preventClick);
    const info = Linkifier._linkInfo(link);
    if (typeof lineNumber === 'number') {
      info.lineNumber = lineNumber;
    }
    if (typeof columnNumber === 'number') {
      info.columnNumber = columnNumber;
    }
    return link;
  }

  /**
   * @param {!Object} revealable
   * @param {string} text
   * @param {string=} fallbackHref
   * @return {!Element}
   */
  static linkifyRevealable(revealable, text, fallbackHref) {
    const link = Linkifier._createLink(text, '', UI.MaxLengthForDisplayedURLs, undefined, fallbackHref);
    Linkifier._linkInfo(link).revealable = revealable;
    return link;
  }

  /**
   * @param {string} text
   * @param {string} className
   * @param {number=} maxLength
   * @param {string=} title
   * @param {string=} href
   * @param {boolean=} preventClick
   * @returns{!Element}
   */
  static _createLink(text, className, maxLength, title, href, preventClick) {
    const link = createElementWithClass('span', className);
    link.classList.add('devtools-link');
    if (title) {
      link.title = title;
    }
    if (href) {
      link.href = href;
    }
    Linkifier._setTrimmedText(link, text, maxLength);
    link[_infoSymbol] = {
      icon: null,
      enableDecorator: false,
      uiLocation: null,
      liveLocation: null,
      url: href || null,
      lineNumber: null,
      columnNumber: null,
      revealable: null,
      fallback: null
    };
    if (!preventClick) {
      link.addEventListener('click', event => {
        if (Linkifier._handleClick(event)) {
          event.consume(true);
        }
      }, false);
      link.addEventListener('keydown', event => {
        if (isEnterKey(event) && Linkifier._handleClick(event)) {
          event.consume(true);
        }
      }, false);
    } else {
      link.classList.add('devtools-link-prevent-click');
    }
    UI.ARIAUtils.markAsLink(link);
    return link;
  }

  /**
   * @param {!Element} link
   * @param {string} text
   * @param {number=} maxLength
   */
  static _setTrimmedText(link, text, maxLength) {
    link.removeChildren();
    if (maxLength && text.length > maxLength) {
      const middleSplit = splitMiddle(text, maxLength);
      appendTextWithoutHashes(middleSplit[0]);
      appendHiddenText(middleSplit[1]);
      appendTextWithoutHashes(middleSplit[2]);
    } else {
      appendTextWithoutHashes(text);
    }

    /**
     * @param {string} string
     */
    function appendHiddenText(string) {
      const ellipsisNode = link.createChild('span', 'devtools-link-ellipsis').createTextChild('\u2026');
      ellipsisNode[_untruncatedNodeTextSymbol] = string;
    }

    /**
     * @param {string} string
     */
    function appendTextWithoutHashes(string) {
      const hashSplit = TextUtils.TextUtils.splitStringByRegexes(string, [/[a-f0-9]{20,}/g]);
      for (const match of hashSplit) {
        if (match.regexIndex === -1) {
          link.createTextChild(match.value);
        } else {
          link.createTextChild(match.value.substring(0, 7));
          appendHiddenText(match.value.substring(7));
        }
      }
    }

    /**
     * @param {string} string
     * @param {number} maxLength
     * @return {!Array<string>}
     */
    function splitMiddle(string, maxLength) {
      let leftIndex = Math.floor(maxLength / 2);
      let rightIndex = string.length - Math.ceil(maxLength / 2) + 1;

      // Do not truncate between characters that use multiple code points (emojis).
      if (string.codePointAt(rightIndex - 1) >= 0x10000) {
        rightIndex++;
        leftIndex++;
      }
      if (leftIndex > 0 && string.codePointAt(leftIndex - 1) >= 0x10000) {
        leftIndex--;
      }
      return [string.substring(0, leftIndex), string.substring(leftIndex, rightIndex), string.substring(rightIndex)];
    }
  }

  /**
   * @param {!Node} node
   * @return {string}
   */
  static untruncatedNodeText(node) {
    return node[_untruncatedNodeTextSymbol] || node.textContent;
  }

  /**
   * @param {?Element} link
   * @return {?Components._LinkInfo}
   */
  static _linkInfo(link) {
    return /** @type {?Components._LinkInfo} */ (link ? link[_infoSymbol] || null : null);
  }

  /**
   * @param {!Event} event
   * @return {boolean}
   */
  static _handleClick(event) {
    const link = /** @type {!Element} */ (event.currentTarget);
    if (UI.isBeingEdited(/** @type {!Node} */ (event.target)) || link.hasSelection()) {
      return false;
    }
    const actions = Linkifier._linkActions(link);
    if (actions.length) {
      actions[0].handler.call(null);
      return true;
    }
    return false;
  }

  /**
   * @return {!Common.Setting}
   */
  static _linkHandlerSetting() {
    if (!Linkifier._linkHandlerSettingInstance) {
      Linkifier._linkHandlerSettingInstance = Common.settings.createSetting('openLinkHandler', ls`auto`);
    }
    return Linkifier._linkHandlerSettingInstance;
  }

  /**
   * @param {string} title
   * @param {!Linkifier.LinkHandler} handler
   */
  static registerLinkHandler(title, handler) {
    _linkHandlers.set(title, handler);
    self.runtime.sharedInstance(LinkHandlerSettingUI)._update();
  }

  /**
   * @param {string} title
   */
  static unregisterLinkHandler(title) {
    _linkHandlers.delete(title);
    self.runtime.sharedInstance(LinkHandlerSettingUI)._update();
  }

  /**
   * @param {!Element} link
   * @return {?Workspace.UILocation}
   */
  static uiLocation(link) {
    const info = Linkifier._linkInfo(link);
    return info ? info.uiLocation : null;
  }

  /**
   * @param {?Element} link
   * @return {!Array<{title: string, handler: function()}>}
   */
  static _linkActions(link) {
    const info = Linkifier._linkInfo(link);
    const result = [];
    if (!info) {
      return result;
    }

    let url = '';
    let uiLocation = null;
    if (info.uiLocation) {
      uiLocation = info.uiLocation;
      url = uiLocation.uiSourceCode.contentURL();
    } else if (info.url) {
      url = info.url;
      const uiSourceCode = Workspace.workspace.uiSourceCodeForURL(url) ||
          Workspace.workspace.uiSourceCodeForURL(Common.ParsedURL.urlWithoutHash(url));
      uiLocation = uiSourceCode ? uiSourceCode.uiLocation(info.lineNumber || 0, info.columnNumber || 0) : null;
    }
    const resource = url ? Bindings.resourceForURL(url) : null;
    const contentProvider = uiLocation ? uiLocation.uiSourceCode : resource;

    const revealable = info.revealable || uiLocation || resource;
    if (revealable) {
      const destination = Common.Revealer.revealDestination(revealable);
      result.push({
        section: 'reveal',
        title: destination ? ls`Reveal in ${destination}` : ls`Reveal`,
        handler: () => Common.Revealer.reveal(revealable)
      });
    }
    if (contentProvider) {
      const lineNumber = uiLocation ? uiLocation.lineNumber : info.lineNumber || 0;
      for (const title of _linkHandlers.keys()) {
        const handler = _linkHandlers.get(title);
        const action = {
          section: 'reveal',
          title: Common.UIString('Open using %s', title),
          handler: handler.bind(null, contentProvider, lineNumber)
        };
        if (title === Linkifier._linkHandlerSetting().get()) {
          result.unshift(action);
        } else {
          result.push(action);
        }
      }
    }
    if (resource || info.url) {
      result.push({
        section: 'reveal',
        title: UI.openLinkExternallyLabel(),
        handler: () => Host.InspectorFrontendHost.openInNewTab(url)
      });
      result.push({
        section: 'clipboard',
        title: UI.copyLinkAddressLabel(),
        handler: () => Host.InspectorFrontendHost.copyText(url)
      });
    }
    return result;
  }
}

/** @type {!Set<!Components.Linkifier>} */
export const _instances = new Set();

/** @type {?LinkDecorator} */
export let _decorator = null;

export const _sourceCodeAnchors = Symbol('Linkifier.anchors');
export const _infoSymbol = Symbol('Linkifier.info');
export const _untruncatedNodeTextSymbol = Symbol('Linkifier.untruncatedNodeText');

/**
 * The maximum length before strings are considered too long for finding URLs.
 * @const
 * @type {number}
 */
export const MaxLengthToIgnoreLinkifier = 10000;

/** @type {!Map<string, !Linkifier.LinkHandler>} */
export const _linkHandlers = new Map();

/**
 * @extends {Common.EventTarget}
 * @interface
 */
export class LinkDecorator {
  /**
   * @param {!Workspace.UISourceCode} uiSourceCode
   * @return {?UI.Icon}
   */
  linkIcon(uiSourceCode) {}
}

LinkDecorator.Events = {
  LinkIconChanged: Symbol('LinkIconChanged')
};

/**
 * @implements {UI.ContextMenu.Provider}
 * @unrestricted
 */
export class LinkContextMenuProvider {
  /**
   * @override
   * @param {!Event} event
   * @param {!UI.ContextMenu} contextMenu
   * @param {!Object} target
   */
  appendApplicableItems(event, contextMenu, target) {
    let targetNode = /** @type {!Node} */ (target);
    while (targetNode && !targetNode[_infoSymbol]) {
      targetNode = targetNode.parentNodeOrShadowHost();
    }
    const link = /** @type {?Element} */ (targetNode);
    const actions = Linkifier._linkActions(link);
    for (const action of actions) {
      contextMenu.section(action.section).appendItem(action.title, action.handler);
    }
  }
}

/**
 * @implements {UI.SettingUI}
 * @unrestricted
 */
export class LinkHandlerSettingUI {
  constructor() {
    this._element = createElementWithClass('select', 'chrome-select');
    this._element.addEventListener('change', this._onChange.bind(this), false);
    this._update();
  }

  _update() {
    this._element.removeChildren();
    const names = _linkHandlers.keysArray();
    names.unshift(Common.UIString('auto'));
    for (const name of names) {
      const option = createElement('option');
      option.textContent = name;
      option.selected = name === Linkifier._linkHandlerSetting().get();
      this._element.appendChild(option);
    }
    this._element.disabled = names.length <= 1;
  }

  /**
   * @param {!Event} event
   */
  _onChange(event) {
    const value = event.target.value;
    Linkifier._linkHandlerSetting().set(value);
  }

  /**
   * @override
   * @return {?Element}
   */
  settingElement() {
    return UI.SettingsUI.createCustomSetting(Common.UIString('Link handling:'), this._element);
  }
}

/**
 * @implements {UI.ContextMenu.Provider}
 * @unrestricted
 */
export class ContentProviderContextMenuProvider {
  /**
   * @override
   * @param {!Event} event
   * @param {!UI.ContextMenu} contextMenu
   * @param {!Object} target
   */
  appendApplicableItems(event, contextMenu, target) {
    const contentProvider = /** @type {!Common.ContentProvider} */ (target);
    if (!contentProvider.contentURL()) {
      return;
    }

    contextMenu.revealSection().appendItem(
        UI.openLinkExternallyLabel(), () => Host.InspectorFrontendHost.openInNewTab(contentProvider.contentURL()));
    for (const title of _linkHandlers.keys()) {
      const handler = _linkHandlers.get(title);
      contextMenu.revealSection().appendItem(
          Common.UIString('Open using %s', title), handler.bind(null, contentProvider, 0));
    }
    if (contentProvider instanceof SDK.NetworkRequest) {
      return;
    }

    contextMenu.clipboardSection().appendItem(
        UI.copyLinkAddressLabel(), () => Host.InspectorFrontendHost.copyText(contentProvider.contentURL()));
  }
}

/* Legacy exported object */
self.Components = self.Components || {};

/* Legacy exported object */
Components = Components || {};

/** @constructor */
Components.Linkifier = Linkifier;

Components.Linkifier._instances = _instances;
Components.Linkifier._decorator = _decorator;
Components.Linkifier._sourceCodeAnchors = _sourceCodeAnchors;
Components.Linkifier._infoSymbol = _infoSymbol;
Components.Linkifier._untruncatedNodeTextSymbol = _untruncatedNodeTextSymbol;
Components.Linkifier.MaxLengthToIgnoreLinkifier = MaxLengthToIgnoreLinkifier;
Components.Linkifier._linkHandlers = _linkHandlers;

/** @constructor */
Components.Linkifier.LinkContextMenuProvider = LinkContextMenuProvider;

/** @constructor */
Components.Linkifier.LinkHandlerSettingUI = LinkHandlerSettingUI;

/** @constructor */
Components.Linkifier.ContentProviderContextMenuProvider = ContentProviderContextMenuProvider;

/** @interface */
Components.LinkDecorator = LinkDecorator;

/**
 * @typedef {{
  *     icon: ?UI.Icon,
  *     enableDecorator: boolean,
  *     uiLocation: ?Workspace.UILocation,
  *     liveLocation: ?Bindings.LiveLocation,
  *     url: ?string,
  *     lineNumber: ?number,
  *     columnNumber: ?number,
  *     revealable: ?Object,
  *     fallback: ?Element
  * }}
  */
Components._LinkInfo;

/**
  * @typedef {{
  *     text: (string|undefined),
  *     className: (string|undefined),
  *     lineNumber: (number|undefined),
  *     columnNumber: (number|undefined),
  *     preventClick: (boolean|undefined),
  *     maxLength: (number|undefined)
  * }}
  */
Components.LinkifyURLOptions;

/**
 * @typedef {function(!Common.ContentProvider, number)}
 */
Components.Linkifier.LinkHandler;