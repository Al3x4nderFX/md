#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// ---------------------------------------------------------------
// Guardado / lectura de progreso
// Se combina el porcentaje REAL que ya guarda el juego en el nivel
// (m_normalPercent / m_practicePercent) con el que guarda este mod.
// ---------------------------------------------------------------
static std::string progressKey(GJGameLevel* level, bool practice) {
    return fmt::format(
        "lvl_{}_{}_{}",
        level->m_levelID.value(),
        std::string(level->m_levelName),
        practice ? "p" : "n"
    );
}

static int loadProgress(GJGameLevel* level, bool practice) {
    int saved = Mod::get()->getSavedValue<int>(progressKey(level, practice), 0);
    int game = practice
        ? static_cast<int>(level->m_practicePercent)
        : static_cast<int>(level->m_normalPercent.value());
    return std::clamp(std::max(saved, game), 0, 100);
}

static void storeProgress(GJGameLevel* level, bool practice, int pct) {
    auto key = progressKey(level, practice);
    auto mod = Mod::get();
    if (pct > mod->getSavedValue<int>(key, 0)) {
        mod->setSavedValue<int>(key, std::clamp(pct, 0, 100));
    }
}

// ---------------------------------------------------------------
// Una fila: título encima + barra (assets originales del juego:
// GJ_progressBar_001.png y bigFont.fnt). Las posiciones son relativas
// a la esquina inferior izquierda de "root"; "y" es la base de la fila.
// ---------------------------------------------------------------
static constexpr float TITLE_SCALE = 0.4f;
static constexpr float TITLE_GAP = 2.f;

static void addRow(
    CCNode* root, const char* title, int pct, ccColor3B color,
    CCSize barSize, float y, float titleH
) {
    float midX = barSize.width / 2.f;
    float barMidY = y + barSize.height / 2.f;

    // Fondo de la barra: textura original, oscura y semitransparente
    auto back = CCSprite::create("GJ_progressBar_001.png");
    if (back) {
        back->setColor({0, 0, 0});
        back->setOpacity(125);
        back->setPosition({midX, barMidY});
        root->addChild(back, 1);
    }

    // Relleno: misma textura, coloreada y recortada según el porcentaje.
    // setTextureRect usa PUNTOS (igual que getContentSize).
    if (pct > 0) {
        auto fill = CCSprite::create("GJ_progressBar_001.png");
        if (fill) {
            fill->setColor(color);
            fill->setAnchorPoint({0.f, 0.5f});
            fill->setTextureRect(CCRect(0.f, 0.f, barSize.width * pct / 100.f, barSize.height));
            fill->setScaleX(0.992f);
            fill->setScaleY(0.86f);
            fill->setPosition({barSize.width * (1.f - 0.992f) / 2.f, barMidY});
            root->addChild(fill, 2);
        }
    }

    // Porcentaje centrado dentro de la barra
    if (Mod::get()->getSettingValue<bool>("show-text")) {
        auto percent = CCLabelBMFont::create(fmt::format("{}%", pct).c_str(), "bigFont.fnt");
        percent->setScale(0.45f);
        percent->setPosition({midX, barMidY});
        root->addChild(percent, 4);
    }

    // Título encima de la barra
    auto titleLabel = CCLabelBMFont::create(title, "bigFont.fnt");
    titleLabel->setScale(TITLE_SCALE);
    titleLabel->setPosition({midX, y + barSize.height + TITLE_GAP + titleH / 2.f});
    root->addChild(titleLabel, 3);
}

// Dos filas apiladas: Normal Mode (arriba) y Practice Mode (abajo)
static CCNode* buildProgressBar(int normal, int practice) {
    auto probe = CCSprite::create("GJ_progressBar_001.png");
    CCSize barSize = probe ? probe->getContentSize() : CCSize(220.f, 20.f);

    auto probeLabel = CCLabelBMFont::create("Normal Mode", "bigFont.fnt");
    float titleH = probeLabel->getContentSize().height * TITLE_SCALE;

    constexpr float rowGap = 5.f;
    float rowH = barSize.height + TITLE_GAP + titleH;

    auto root = CCNode::create();
    root->setAnchorPoint({0.5f, 0.5f});
    root->setContentSize({barSize.width, rowH * 2.f + rowGap});

    addRow(root, "Normal Mode", normal, {0, 255, 0}, barSize, rowH + rowGap, titleH);
    addRow(root, "Practice Mode", practice, {0, 255, 255}, barSize, 0.f, titleH);
    return root;
}

// ---------------------------------------------------------------
// Busca los botones grandes (editar / jugar / compartir) sin depender de IDs
// ---------------------------------------------------------------
static void collectButtons(CCNode* node, std::vector<CCNode*>& out) {
    auto children = node->getChildren();
    if (!children) return;
    for (auto child : CCArrayExt<CCNode*>(children)) {
        if (typeinfo_cast<CCMenuItemSpriteExtra*>(child)) {
            out.push_back(child);
        } else {
            collectButtons(child, out);
        }
    }
}

// ---------------------------------------------------------------
// Pantalla del nivel (editar / jugar / compartir)
// ---------------------------------------------------------------
class $modify(ProgressEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        auto winSize = CCDirector::get()->getWinSize();
        float cx = winSize.width / 2.f;

        // 1) Bajar los 3 botones grandes para hacer espacio arriba
        float shift = winSize.height * 0.10f;
        float buttonsTop = winSize.height * 0.40f; // valor por defecto si no se encuentran

        std::vector<CCNode*> all;
        collectButtons(this, all);

        bool moved = false;
        float topMost = 0.f;
        for (auto btn : all) {
            auto parent = btn->getParent();
            if (!parent) continue;

            auto wp = parent->convertToWorldSpace(btn->getPosition());
            auto lp = this->convertToNodeSpace(wp);

            // Fila central de botones: ni la carpeta, ni los de los lados
            bool inRow = lp.y > winSize.height * 0.30f && lp.y < winSize.height * 0.58f;
            bool inCenter = std::abs(lp.x - cx) < winSize.width * 0.25f;
            if (!inRow || !inCenter) continue;

            auto p0 = parent->convertToNodeSpace(wp);
            auto p1 = parent->convertToNodeSpace(wp + CCPoint(0.f, -shift));
            btn->setPosition(btn->getPosition() + (p1 - p0));

            // Parte superior del botón, ya movido (en coordenadas de esta capa)
            auto box = btn->boundingBox();
            auto top = this->convertToNodeSpace(
                parent->convertToWorldSpace(CCPoint(box.getMidX(), box.getMaxY()))
            );
            topMost = moved ? std::max(topMost, top.y) : top.y;
            moved = true;
        }
        if (moved) buttonsTop = topMost;

        // 2) Crear las barras y colocarlas en el hueco entre la descripción y los botones
        auto bar = buildProgressBar(loadProgress(level, false), loadProgress(level, true));
        bar->setID("progress-bar"_spr);

        float topLimit = winSize.height * 0.60f;   // justo debajo del cuadro de descripción
        float bottomLimit = buttonsTop + 3.f;      // justo encima de los botones
        float available = std::max(topLimit - bottomLimit, 10.f);

        float userScale = static_cast<float>(Mod::get()->getSettingValue<double>("bar-scale"));
        float nativeH = std::max(bar->getContentSize().height, 1.f);
        float scale = std::min(0.85f * userScale, available / nativeH); // nunca invade botones ni descripción
        bar->setScale(scale);

        bar->setPosition({cx, (topLimit + bottomLimit) / 2.f});
        this->addChild(bar, 10);

        return true;
    }
};

// ---------------------------------------------------------------
// Al jugar el nivel: guardar el mejor porcentaje (normal / práctica)
// ---------------------------------------------------------------
class $modify(ProgressPlayLayer, PlayLayer) {
    struct Fields {
        int m_lastPct = -1;
        bool m_lastPractice = false;
    };

    bool isLocalLevel() {
        return m_level && m_level->m_levelType == GJLevelType::Editor;
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();
        if (!this->isLocalLevel()) return;

        int pct = std::clamp(static_cast<int>(this->getCurrentPercent()), 0, 100);
        bool practice = m_isPracticeMode;

        if (pct != m_fields->m_lastPct || practice != m_fields->m_lastPractice) {
            m_fields->m_lastPct = pct;
            m_fields->m_lastPractice = practice;
            storeProgress(m_level, practice, pct);
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (!this->isLocalLevel()) return;
        storeProgress(m_level, m_isPracticeMode, 100);
        (void) Mod::get()->saveData();
    }

    void onQuit() {
        PlayLayer::onQuit();
        if (this->isLocalLevel()) (void) Mod::get()->saveData();
    }
};
