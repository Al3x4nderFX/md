#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// ---------------------------------------------------------------
// Utilidades de guardado (un valor por nivel: normal y práctica)
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
    return Mod::get()->getSavedValue<int>(progressKey(level, practice), 0);
}

static void storeProgress(GJGameLevel* level, bool practice, int pct) {
    auto key = progressKey(level, practice);
    auto mod = Mod::get();
    if (pct > mod->getSavedValue<int>(key, 0)) {
        mod->setSavedValue<int>(key, std::clamp(pct, 0, 100));
    }
}

// ---------------------------------------------------------------
// Una barra con los assets originales del juego
// (la misma textura que usan las barras de niveles online)
// ---------------------------------------------------------------
static CCNode* makeGameBar(int pct, ccColor3B color) {
    auto node = CCNode::create();
    node->setAnchorPoint({0.5f, 0.5f});

    // Fondo: textura original de la barra, oscura y semitransparente
    auto back = CCSprite::create("GJ_progressBar_001.png");
    if (!back) return node;
    back->setColor({0, 0, 0});
    back->setOpacity(125);

    auto size = back->getContentSize();
    node->setContentSize(size);
    back->setPosition({size.width / 2.f, size.height / 2.f});
    node->addChild(back, 1);

    // Relleno: la misma textura, coloreada y recortada según el porcentaje
    if (pct > 0) {
        auto fill = CCSprite::create("GJ_progressBar_001.png");
        if (fill) {
            fill->setColor(color);
            fill->setScaleX(0.992f);
            fill->setScaleY(0.86f);
            fill->setAnchorPoint({0.f, 0.5f});
            fill->setPosition({size.width * (1.f - 0.992f) / 2.f, size.height / 2.f});

            // setTextureRect usa píxeles, así que se multiplica por el factor de contenido
            float f = CCDirector::get()->getContentScaleFactor();
            fill->setTextureRect(CCRect(0.f, 0.f, size.width * f * pct / 100.f, size.height * f));
            node->addChild(fill, 2);
        }
    }

    // Porcentaje con la fuente original del juego
    if (Mod::get()->getSettingValue<bool>("show-text")) {
        auto label = CCLabelBMFont::create(fmt::format("{}%", pct).c_str(), "bigFont.fnt");
        label->setScale(0.5f);
        label->setPosition({size.width / 2.f, size.height / 2.f});
        node->addChild(label, 3);
    }

    return node;
}

// Dos barras apiladas: normal (verde) y práctica (azul)
static CCNode* buildProgressBar(int normal, int practice) {
    auto normalBar = makeGameBar(normal, {0, 255, 0});
    auto practiceBar = makeGameBar(practice, {0, 255, 255});

    auto size = normalBar->getContentSize();
    constexpr float gap = 4.f;

    auto root = CCNode::create();
    root->setAnchorPoint({0.5f, 0.5f});
    root->setContentSize({size.width, size.height * 2.f + gap});

    normalBar->setPosition({size.width / 2.f, size.height * 1.5f + gap});
    practiceBar->setPosition({size.width / 2.f, size.height / 2.f});
    root->addChild(normalBar);
    root->addChild(practiceBar);
    return root;
}

// ---------------------------------------------------------------
// Pantalla del nivel (editar / jugar / compartir): mostrar la barra
// ---------------------------------------------------------------
class $modify(ProgressEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        auto winSize = CCDirector::get()->getWinSize();
        auto bar = buildProgressBar(loadProgress(level, false), loadProgress(level, true));
        bar->setID("progress-bar"_spr);

        // Ancho base ~150 unidades, ajustable con el ajuste "Tamaño de la barra"
        float userScale = static_cast<float>(Mod::get()->getSettingValue<double>("bar-scale"));
        float width = std::max(bar->getContentSize().width, 1.f);
        bar->setScale(userScale * 150.f / width);

        // Justo debajo de los 3 botones (editar / jugar / compartir)
        bar->setPosition({winSize.width / 2.f, winSize.height * 0.26f});
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
