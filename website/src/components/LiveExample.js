import {useEffect, useState} from 'react';
import Link from '@docusaurus/Link';
import useBaseUrl from '@docusaurus/useBaseUrl';
import {ensureIsolated} from './StylePreview/engine';
import manifest from '@site/../docs/examples/examples.json';

/**
 * An example running, in its own page: the SDK module is one map per page, and the runner is the
 * same page an app would host (web/examples/run.html). The iframe is only cross-origin isolated
 * when this page is, which the site's service worker arranges once.
 */
export function LiveMap({example}) {
  const runner = useBaseUrl(`/massif/examples/run.html?id=${example.id}`);
  const worker = useBaseUrl('/coi-serviceworker.js');
  const [state, setState] = useState({ready: false, reason: ''});
  useEffect(() => {
    ensureIsolated(worker).then(({isolated, reason}) => setState({ready: isolated, reason: reason ?? ''}));
  }, [worker]);
  return (
    <div className="exampleLive">
      {state.ready ? (
        <iframe src={runner} title={example.title} loading="lazy" allow="cross-origin-isolated; fullscreen" />
      ) : (
        <div className="exampleLiveWait">{state.reason || 'Starting…'}</div>
      )}
      <a href={runner} target="_blank" rel="noopener">Open full screen ↗</a>
    </div>
  );
}

/** A gallery example embedded in a docs page, by its id in docs/examples/examples.json. */
export default function LiveExample({id}) {
  const example = manifest.sections.flatMap((section) => section.examples).find((entry) => entry.id === id);
  if (!example?.live) {
    throw new Error(`No live example "${id}" in docs/examples/examples.json`);
  }
  return (
    <figure className="liveExample">
      <LiveMap example={example} />
      <figcaption>
        {example.title}, running live on the web SDK. <Link to={`/examples#${id}`}>The code →</Link>
      </figcaption>
    </figure>
  );
}
