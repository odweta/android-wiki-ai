package org.odweta.androidwikiai;

import org.odweta.androidwikiai.offlineai.LlamaModel;
import org.odweta.androidwikiai.offlineai.ZimArchive;

import java.io.Closeable;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * The grounded answer pipeline: search the imported ZIM for relevant
 * articles, fail closed if none are found (or the files aren't imported),
 * otherwise prompt the loaded GGUF model with the retrieved excerpts as
 * context and return the answer together with which articles it used.
 *
 * <p>Everything here runs fully offline: no network access is used or
 * required at runtime.
 */
public final class AssistantController implements Closeable {
    private static final int CONTEXT_SIZE = 2048;
    private static final int MAX_ANSWER_TOKENS = 256;
    private static final int MAX_ARTICLES = 3;
    private static final int MAX_EXCERPT_CHARS = 1200;
    private static final String REFUSAL = "I don't know. I couldn't find a relevant offline "
            + "Wikipedia article to ground an answer in, so I won't guess.";

    /** The result of {@link #ask}: an answer plus the articles it cites, if any. */
    public static final class Answer {
        public final String text;
        public final List<String> citations;

        Answer(String text, List<String> citations) {
            this.text = text;
            this.citations = citations;
        }
    }

    private final File modelFile;
    private final File zimFile;
    private ZimArchive archive;
    private LlamaModel model;

    public AssistantController(File modelFile, File zimFile) {
        this.modelFile = modelFile;
        this.zimFile = zimFile;
    }

    /**
     * Answers {@code question}, grounded in the imported ZIM archive. Must
     * be called off the UI thread: it performs file I/O and on-device
     * inference.
     */
    public Answer ask(String question) {
        if (question == null || question.trim().isEmpty()) {
            return refusal();
        }
        if (!modelFile.exists()) {
            return new Answer("Import a .gguf model first; the assistant has nothing to run "
                    + "inference with yet.", Collections.emptyList());
        }
        if (!zimFile.exists()) {
            return new Answer("Import a .zim Wikipedia dump first; there is no offline article "
                    + "source to ground an answer in yet.", Collections.emptyList());
        }

        ZimArchive.Article[] articles;
        try {
            ensureArchiveOpen();
            articles = archive.search(question, MAX_ARTICLES);
        } catch (IOException exception) {
            return new Answer("Could not read the imported ZIM file: " + exception.getMessage(),
                    Collections.emptyList());
        }
        if (articles.length == 0) {
            return refusal();
        }

        String generated;
        try {
            ensureModelLoaded();
            String prompt = buildPrompt(question, articles);
            generated = model.generate(prompt, MAX_ANSWER_TOKENS).trim();
        } catch (IOException exception) {
            return new Answer("Could not run the imported model: " + exception.getMessage(),
                    Collections.emptyList());
        }
        if (generated.isEmpty()) {
            return refusal();
        }

        List<String> citations = new ArrayList<>();
        for (ZimArchive.Article article : articles) {
            citations.add(article.title);
        }
        return new Answer(generated, citations);
    }

    private Answer refusal() {
        return new Answer(REFUSAL, Collections.emptyList());
    }

    private void ensureArchiveOpen() throws IOException {
        if (archive == null) {
            archive = new ZimArchive(zimFile.getAbsolutePath());
        }
    }

    private void ensureModelLoaded() throws IOException {
        if (model == null) {
            model = new LlamaModel(modelFile.getAbsolutePath(), CONTEXT_SIZE);
        }
    }

    private String buildPrompt(String question, ZimArchive.Article[] articles) {
        StringBuilder prompt = new StringBuilder();
        prompt.append("You are an offline assistant. Answer the question using ONLY the "
                + "article excerpts below. If the excerpts do not contain the answer, say you "
                + "don't know instead of guessing.\n\n");
        for (ZimArchive.Article article : articles) {
            prompt.append("Article: ").append(article.title).append('\n');
            String content = article.content == null ? "" : article.content;
            String excerpt = content.length() > MAX_EXCERPT_CHARS
                    ? content.substring(0, MAX_EXCERPT_CHARS)
                    : content;
            prompt.append(excerpt).append("\n\n");
        }
        prompt.append("Question: ").append(question).append("\nAnswer:");
        return prompt.toString();
    }

    @Override
    public void close() {
        if (archive != null) {
            archive.close();
            archive = null;
        }
        if (model != null) {
            model.close();
            model = null;
        }
    }
}
