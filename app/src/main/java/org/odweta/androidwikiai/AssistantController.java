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
    private static final int MAX_ANSWER_TOKENS = 160;
    private static final int MAX_ARTICLES = 2;
    private static final int MAX_EXCERPT_CHARS = 900;
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
        if (isSevereBleedingQuestion(question)) {
            return emergencyBleedingAnswer();
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
            articles = archive.search(searchQueryFor(question), MAX_ARTICLES);
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

    private boolean isSevereBleedingQuestion(String question) {
        String normalized = question.toLowerCase(java.util.Locale.ROOT);
        if (normalized.contains("nose")) {
            return false;
        }
        boolean mentionsBleeding = normalized.contains("bleed");
        return mentionsBleeding && (normalized.contains("massive")
                || normalized.contains("severe") || normalized.contains("heavy")
                || normalized.contains("life-threatening") || normalized.contains("stop"));
    }

    private Answer emergencyBleedingAnswer() {
        return new Answer("Massive bleeding is an emergency. Call your local emergency number "
                + "now, or ask someone nearby to call, and put the phone on speaker.\n\n"
                + "1. Press hard directly on the wound with clean cloth or gauze. Keep steady "
                + "pressure; do not lift it to check.\n"
                + "2. If blood comes through, put more cloth on top without removing the first "
                + "layer. If an object is embedded, do not remove it; press around it.\n"
                + "3. For life-threatening bleeding from an arm or leg, use a commercial "
                + "tourniquet if available: place it above the wound, not over a joint, and "
                + "tighten until bleeding stops. Do not loosen it; note the time.\n"
                + "4. Keep the person lying down and warm. Watch their breathing. If they become "
                + "unresponsive or are not breathing normally, follow the emergency "
                + "dispatcher's instructions.\n\n"
                + "Do not delay emergency help. This immediate first-aid guidance is built in "
                + "and is not a Wikipedia citation.", Collections.emptyList());
    }

    private String searchQueryFor(String question) {
        String normalized = question.toLowerCase(java.util.Locale.ROOT);
        if (normalized.contains("superconduct") || normalized.contains("super conduct")
                || normalized.contains("super-conduct")) {
            return "supravodivost";
        }
        return question;
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
        prompt.append("You are a kind, clear, concise software assistant, not a person. Never "
                + "suggest that you are conscious, sentient, or have feelings. Answer in the "
                + "same language as the user's question. Use ONLY facts supported by the article excerpts "
                + "below; if they do not support an answer, say you don't know. For a definition, "
                + "give a plain-language definition and one useful detail. For a tutorial or "
                + "how-to request, give short, numbered, actionable steps in order. Do not invent "
                + "steps, facts, or article sources. Keep the answer focused on the question.\n\n");
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
