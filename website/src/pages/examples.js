import {useEffect, useState} from 'react';
import Layout from '@theme/Layout';
import CodeBlock from '@theme/CodeBlock';
import manifest from '@site/../docs/examples/examples.json';

/*
 * The example gallery.
 *
 * Everything on this page is generated: docs/examples/examples.json comes from the demo apps'
 * own @ExampleInfo annotations (scripts/gen-examples.py) and the screenshots from
 * scripts/capture-examples.py. Adding an example to a demo adds it here - there is no list on
 * this side to keep in step.
 */

// The screenshots live in the docs tree, which is the one home for them: the Android build ships
// that directory as an APK asset and this reads the same files.
const shots = require.context('../../../docs/examples/screenshots', false, /\.png$/);

function shotFor(id) {
  try {
    return shots(`./${id}.png`).default ?? shots(`./${id}.png`);
  } catch (e) {
    return null;
  }
}

const LANGUAGES = [
  {key: 'java', label: 'Java (Android)', prism: 'java'},
  {key: 'objc', label: 'Objective-C (iOS)', prism: 'objectivec'},
];

const REPO = 'https://github.com/massif-maps/MassifMaps/blob/master/';

function Card({example, onOpen}) {
  const shot = shotFor(example.id);
  return (
    <div className="col col--4" style={{marginBottom: '1.75rem'}}>
      <div className="exampleCard" onClick={() => onOpen(example.id)}>
        <div className="exampleShot">
          {shot ? (
            <img src={shot} alt={example.title} loading="lazy" />
          ) : (
            <div className="exampleShotMissing">No screenshot yet</div>
          )}
        </div>
        <div className="exampleCardBody">
          <h3>{example.title}</h3>
          <p>{example.description}</p>
        </div>
      </div>
    </div>
  );
}

function Detail({example, onClose}) {
  const available = LANGUAGES.filter((language) => example.code[language.key]);
  const [language, setLanguage] = useState(available[0]?.key ?? 'java');
  const shot = shotFor(example.id);

  useEffect(() => {
    if (!available.some((entry) => entry.key === language)) {
      setLanguage(available[0]?.key);
    }
  }, [example.id]);

  return (
    <div className="exampleDetail">
      <button className="exampleBack" onClick={onClose}>
        ← All examples
      </button>
      <h1>{example.title}</h1>
      <p className="exampleLead">{example.description}</p>

      {shot && (
        <img className="exampleDetailShot" src={shot} alt={example.title} />
      )}

      <div className="exampleTabs">
        {available.map((entry) => (
          <button
            key={entry.key}
            className={entry.key === language ? 'exampleTab exampleTabOn' : 'exampleTab'}
            onClick={() => setLanguage(entry.key)}>
            {entry.label}
          </button>
        ))}
        {available.length < LANGUAGES.length && (
          <span className="exampleTabNote">
            {LANGUAGES.filter((entry) => !example.code[entry.key])
              .map((entry) => entry.label)
              .join(', ')}{' '}
            not ported yet
          </span>
        )}
      </div>

      <CodeBlock
        language={LANGUAGES.find((entry) => entry.key === language)?.prism ?? 'java'}
        showLineNumbers>
        {example.code[language] ?? ''}
      </CodeBlock>

      <p>
        <a href={REPO + example.source}>This example on GitHub</a>
      </p>
    </div>
  );
}

export default function ExamplesPage() {
  const [openId, setOpenId] = useState(null);

  // The id lives in the URL hash, so an example is linkable and the back button works.
  useEffect(() => {
    const read = () => setOpenId(window.location.hash.replace('#', '') || null);
    read();
    window.addEventListener('hashchange', read);
    return () => window.removeEventListener('hashchange', read);
  }, []);

  const open = (id) => {
    window.location.hash = id;
  };
  const close = () => {
    window.history.pushState('', document.title, window.location.pathname);
    setOpenId(null);
  };

  const all = manifest.sections.flatMap((section) => section.examples);
  const current = all.find((example) => example.id === openId);

  return (
    <Layout
      title="Examples"
      description="Every Massif Maps example, with the code that produced it.">
      <main className="container margin-vert--lg">
        {current ? (
          <Detail example={current} onClose={close} />
        ) : (
          <>
            <h1>Examples</h1>
            <p className="exampleLead">
              Each one is a single file in the demo apps, and each screenshot is that file
              running. Adding an example to a demo adds it to this page.
            </p>
            {manifest.sections.map((section) => (
              <section key={section.id} style={{marginTop: '2.5rem'}}>
                <h2>{section.title}</h2>
                <p>{section.description}</p>
                <div className="row">
                  {section.examples.map((example) => (
                    <Card key={example.id} example={example} onOpen={open} />
                  ))}
                </div>
              </section>
            ))}
          </>
        )}
      </main>
    </Layout>
  );
}
